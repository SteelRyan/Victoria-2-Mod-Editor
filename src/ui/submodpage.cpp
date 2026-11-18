// submodpage.py — Tools → Submods & 4K (see submodpage.h).
#include "ui/submodpage.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/submods.h"
#include "map/fourk.h"
#include "map/mapview_data.h"
#include "core/scriptstore.h"
#include "ui/locwin.h"
#include "ui/mapmarkspage.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <exception>
#include <set>

using namespace Qt::StringLiterals;
using widgets::label;
using widgets::Section;
using widgets::StepSlider;

namespace submodpage {

namespace {

QLabel *rich(bool hidden = false)                   // a selectable rich-text label of the "sub" kind
{
    auto *l = new QLabel();
    l->setObjectName(u"sub"_s);
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (hidden)
        l->hide();
    return l;
}

QStringList sorted_names(const QString &dir, bool *ok = nullptr)   // sorted(os.listdir(dir)); ok false: OSError
{
    QFileInfo fi(dir);
    if (ok)
        *ok = fi.isDir();
    if (!fi.isDir())
        return {};
    QStringList names = QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    std::sort(names.begin(), names.end());
    return names;
}

bool nonempty_dir(const QString &d)
{
    return QFileInfo(d).isDir() &&
           !QDir(d).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty();
}

QString cap(const QString &what) { return what.isEmpty() ? what : what.left(1).toUpper() + what.mid(1); }

// the 4K screen's slider steps (C++ only)
const std::vector<double> WIN_STEPS = {1.0, 1.25, 1.5, 1.75, 2.0};
const std::vector<double> MARK_STEPS = {1.0, 1.25, 4.0 / 3.0, 1.5, 1.75, 2.0};
// the province names in 0.1 steps (the user 2026-11-15: "nothing in between" was not how it should be)
const std::vector<double> NAME_STEPS = {1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 2.0};

QStringList step_names(const std::vector<double> &steps)
{
    QStringList out;
    for (double v : steps)
        out << (std::abs(v - 4.0 / 3.0) < 1e-9 ? u"× 1⅓"_s : u"× "_s + QString::number(v, 'g', 4));
    return out;
}

int nearest(const std::vector<double> &steps, double v)
{
    int best = 0;
    for (int i = 1; i < int(steps.size()); ++i)
        if (std::abs(steps[size_t(i)] - v) < std::abs(steps[size_t(best)] - v))
            best = i;
    return best;
}

QString folder_of(const submods::ModfileValues &v)   // (v.get("path") or "").replace("\\", "/").split("/")[-1]
{
    const QString *p = v.other.get(u"path"_s);
    return QString(p ? *p : QString()).replace(u'\\', u'/').split(u'/').last();
}

QString other_or(const submods::ModfileValues &v, const QString &k, const QString &fallback = {})
{
    const QString *p = v.other.get(k);
    return p && !p->isEmpty() ? *p : fallback;
}

}  // namespace

SubmodWorkspace::SubmodWorkspace(moddata::Mod &mod_, QWidget *parent) : QWidget(parent), mod(mod_)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    outer->addWidget(split, 1);

    auto *left = new QWidget();
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(16, 12, 16, 12);
    ll->setSpacing(8);
    ll->addWidget(label(u"Submods & 4K"_s, u"title"_s));
    state = rich();
    ll->addWidget(state);
    holds = rich(true);
    ll->addWidget(holds);
    ll->addSpacing(10);
    ll->addWidget(_bold(u"Make the 4K interface submod"_s));
    ll->addWidget(label(
        u"Victoria 2 draws its interface in fixed pixel sizes: at 3840 × 2160 everything is half as big as at 1920 × "
        u"1080. This makes a submod in which the whole interface is twice as big: every position and size in the "
        u"interface files doubled, every interface picture, event picture, font and mouse pointer at double size — "
        u"this mod's own and, where it has none, the game's. Nothing in this mod is changed."_s,
        u"sub"_s));
    {                                                // (C++ only) the 4K screen and its sliders
        ll->addWidget(label(
            u"The 4K screen below shows the game at 3840 × 2160 with this submod — drag to move over the map, the "
            u"wheel zooms as in the game. Set how big these parts are made; everything else is twice as big (× 2). "
            u"The unit panel shows as when an army is picked."_s,
            u"sub"_s));
        auto *grid = new QGridLayout();
        grid->setHorizontalSpacing(14);
        grid->setVerticalSpacing(2);
        auto slot = [&](int r, int c, const QString &name, const QStringList &steps, int at) {
            grid->addWidget(label(name, u"sub"_s, false), r * 2, c);
            auto *s = new StepSlider(steps, at);
            connect(s, &StepSlider::valueChanged, this, [this](int) { _screen(); });
            grid->addWidget(s, r * 2 + 1, c);
            return s;
        };
        sz_top = slot(0, 0, u"Top bar"_s, step_names(WIN_STEPS), 4);
        sz_unit = slot(0, 1, u"Unit panel (an army picked)"_s, step_names(WIN_STEPS), 4);
        sz_marks = slot(1, 0, u"Army counters and the other map marks"_s, step_names(MARK_STEPS), 2);
        sz_names = slot(1, 1, u"Province names on the map (× 1: as the game)"_s, step_names(NAME_STEPS), 0);
        ll->addLayout(grid);
        screen = new gameview::GameView();
        screen->screen = QSize(3840, 2160);
        screen->title = u"4K screen (3840 × 2160)"_s;
        screen->political = false;
        ll->addWidget(screen);
        sz_note = label(QString(), u"sub"_s);
        sz_note->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ll->addWidget(sz_note);
    }
    auto *row = new QHBoxLayout();
    row->setSpacing(6);
    row->addWidget(label(u"Folder of the submod"_s, u"sub"_s, false));
    folder = new QLineEdit();
    connect(folder, &QLineEdit::textChanged, this, [this](const QString &) { _show(); });
    row->addWidget(folder, 1);
    ll->addLayout(row);
    where = label(QString(), u"sub"_s);
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ll->addWidget(where);
    warn = new QLabel();
    warn->setWordWrap(true);
    warn->setStyleSheet(u"QLabel { color: "_s + theme::WARN + u"; font-weight: 600; }"_s);
    warn->hide();
    ll->addWidget(warn);
    row = new QHBoxLayout();
    row->setSpacing(6);
    make_btn = new QPushButton(u"Make the 4K submod"_s);
    make_btn->setObjectName(u"primary"_s);
    connect(make_btn, &QPushButton::clicked, this, [this] { _make(); });
    row->addWidget(make_btn);
    stop_btn = new QPushButton(u"Stop"_s);
    connect(stop_btn, &QPushButton::clicked, this, [this] { _ask_stop(); });
    stop_btn->hide();
    row->addWidget(stop_btn);
    row->addStretch(1);
    ll->addLayout(row);
    bar = new QProgressBar();
    bar->setTextVisible(false);
    bar->setFixedHeight(12);
    bar->hide();
    ll->addWidget(bar);
    progress = label(QString(), u"sub"_s);
    ll->addWidget(progress);
    result = rich();
    ll->addWidget(result);
    // (C++ only, 2026-11-18, the user's wish) the checksum has a page of its own (Tools → Checksum): this section
    // is kept built (its code still runs) but never shown
    auto *ck_box = new QWidget();
    auto *cl = new QVBoxLayout(ck_box);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addSpacing(14);
    cl->addWidget(_bold(u"Checksum check"_s));
    cl->addWidget(label(
        u"Can people play together when one has a submod and the others don't? The editor works out the game's own "
        u"letters the way the game does (only some folders count), and can start the game to read them: once for the "
        u"mod alone, once with the submod — the game is minimised as soon as its window shows and ended again, about "
        u"half a minute each."_s,
        u"sub"_s));
    ck_files = rich();
    cl->addWidget(ck_files);
    row = new QHBoxLayout();
    row->setSpacing(6);
    ck_btn = new QPushButton(u"Measure in the game"_s);
    connect(ck_btn, &QPushButton::clicked, this, [this] { _measure(); });
    row->addWidget(ck_btn);
    code_btn = new QPushButton(u"Work out the letters (no game start)"_s);
    connect(code_btn, &QPushButton::clicked, this, [this] { _codes(); });
    row->addWidget(code_btn);
    ck_stop = new QPushButton(u"Stop"_s);
    connect(ck_stop, &QPushButton::clicked, this, [this] { _measure_stop(); });
    ck_stop->hide();
    row->addWidget(ck_stop);
    row->addStretch(1);
    cl->addLayout(row);
    ck_result = rich();
    cl->addWidget(ck_result);
    ll->addWidget(ck_box);
    ck_box->hide();
    _ck_timer = new QTimer(this);
    _ck_timer->setInterval(800);
    connect(_ck_timer, &QTimer::timeout, this, [this] { _measure_tick(); });
    ll->addStretch(1);
    auto *lsc = new QScrollArea();
    lsc->setWidgetResizable(true);
    lsc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lsc->setObjectName(u"sideScroll"_s);
    left->setObjectName(u"sideInner"_s);
    lsc->setWidget(left);
    split->addWidget(lsc);

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
    split->setStretchFactor(0, 6);
    split->setStretchFactor(1, 5);
    split->setSizes({950, 800});
    auto section = [&](const QString &head, const QString &text) {
        auto *s = new Section();
        s->lay->addWidget(_bold(head));
        s->lay->addWidget(label(text, u"sub"_s));
        rl->addWidget(s);
    };
    section(u"What a submod is"_s,
            u"A submod holds only what it changes. The game loads it on top of its parent mod (its .mod file names the "
            u"parent): a file of the submod replaces the parent's file of the same name, everything else stays the "
            u"parent's. In the game's launcher tick the parent AND the submod."_s);
    section(u"Making one"_s,
            u"Mod ▾ (top left) → pick the mod in the start window → “New submod of the selected mod…” → give it a name "
            u"→ open it like any mod.\n\n"
            u"Every page works in a submod. Apply writes only the files that differ from the parent into the submod's "
            u"folder; the parent mod is never written to. Take a change back and Apply: the file leaves the submod "
            u"again.\n\n"
            u"Example — a submod that changes nothing but the shortcuts: make the submod, open it, set the keys on "
            u"Rules → Shortcuts, Apply. Its folder then holds the changed interface files and nothing else."_s);
    section(u"Submods and multiplayer (the checksum)"_s,
            u"The four letters in the game's main menu are the checksum; players can only play together when theirs are "
            u"the same. Measured in the game: files in common, history, events, decisions, news, battleplans, units, "
            u"inventions, technologies and poptypes (and map/default.map) change it. Interface files, every picture, "
            u"font and pointer, flags, texts (localisation), music and sound do NOT. So a submod of shortcuts, "
            u"interface or graphics — the 4K submod too — leaves the checksum alone, and only you need it. The editor "
            u"says which kind this submod is, here and under the toolbar."_s);
    section(u"What a submod cannot do"_s,
            u"It cannot delete a file of its parent: the game would still read the parent's. Where the game reads every "
            u"file of a folder (history, events, decisions, texts, units, inventions) the editor puts an empty file of "
            u"the same name into the submod instead — that takes the parent's file out of play. For a war at the start "
            u"(an empty war file crashes the game), a picture or an interface file there is no such way: the editor "
            u"says so in red and the parent's file stays in the game."_s);
    section(u"The 4K submod in the game"_s,
            u"Tick the mod AND its 4K submod in the launcher. With only the submod ticked the game runs without the mod "
            u"— plain Victoria 2 with a big interface — and shows other checksum letters (measured: Modus UPPR, with its "
            u"4K submod UPPR, the 4K submod alone VKEW). The pair has its own settings and saved games (Documents › "
            u"Paradox Interactive › Victoria II › a folder named after both). Made again after the mod changed, it "
            u"follows the mod: only the interface files and pictures are made anew.\n\n"
            u"A picture that would be too big for the game as a plain file (above about 16 MB the game shows black) is "
            u"written packed; the result lists every picture that was written differently than it was."_s);
    rl->addStretch(1);
}

SubmodWorkspace::~SubmodWorkspace() = default;

QLabel *SubmodWorkspace::_bold(const QString &text)
{
    QLabel *h = label(text, u"sub"_s, false);
    QFont f(h->font());
    f.setBold(true);
    h->setFont(f);
    return h;
}

// ------------------------------------------------------------------ where this mod stands
QString SubmodWorkspace::real_root() const { return mod.submod ? mod.submod->root : mod.root; }

QString SubmodWorkspace::mods_dir() const { return pypath::dirname(real_root()); }

QStringList SubmodWorkspace::chain_names() const
{
    if (mod.submod)
        return mod.submod->parents + QStringList{mod.submod->name};
    return {mod.name()};
}

void SubmodWorkspace::refresh(document::Document *d)
{
    doc = d;
    const submods::Submod *sub = mod.submod.get();
    if (sub) {
        state->setText(u"<b>"_s + sub->name + u"</b> is a <b>submod of "_s + sub->parents.join(u", "_s) +
                       u"</b>.<br>What you Apply goes into <b>"_s + sub->root +
                       u"</b> — only the files that differ from the parent. The parent mod is only read.<br><span "
                       u"style='color:"_s +
                       theme::INK_FAINT + u"'>The editor works on a merged copy of both in "_s + sub->work +
                       u".</span>"_s);
        const auto files = submods::holds(sub->root);
        const auto [same, says] = submods::verdict(files);
        QStringList rows;
        for (size_t i = 0; i < files.size() && i < 40; ++i) {
            const auto &[rel, c, size_] = files[i];
            rows << u"<span style='color:"_s + (c ? theme::WARN : theme::INK_FAINT) + u"'>"_s +
                        (c ? u"changes the game"_s : u"looks only"_s) + u"</span> "_s + rel;
        }
        const QString modfile = sub->root + u".mod"_s;
        QString modtext;
        QFile f(modfile);
        if (f.open(QIODevice::ReadOnly)) {
            modtext = QString::fromLatin1(f.readAll());
            modtext.replace(u"\r\n"_s, u"\n"_s).replace(u'\r', u'\n');   // (text mode: universal newlines)
            modtext = py::strip(modtext).replace(u"\n"_s, u"<br>"_s);
        } else {
            modtext = u"(not found)"_s;
        }
        const qsizetype n = qsizetype(files.size());
        holds->setText(u"<b>Its .mod file</b> ("_s + pypath::basename(modfile) +
                       u") — the line “dependencies” is what makes it a submod:<br><span style='font-family:"_s +
                       theme::MONO + u"'>"_s + modtext + u"</span><br><br><b>What it holds now: "_s +
                       QString::number(n) + u" file"_s + (n == 1 ? QString() : u"s"_s) +
                       u"</b> — <span style='color:"_s + (same ? theme::OK : theme::WARN) +
                       u"; font-weight:600'>"_s + says + u"</span><br>"_s +
                       (!rows.isEmpty() ? rows.join(u"<br>"_s) : u"nothing yet: Apply writes the first file"_s) +
                       (n > 40 ? u"<br>… and "_s + QString::number(n - 40) + u" more"_s : QString()));
    }
    holds->setVisible(sub != nullptr);
    if (!sub) {
        QStringList here;
        bool ok = false;
        for (const QString &fn : sorted_names(mods_dir(), &ok))
            if (py::lower(fn).endsWith(u".mod")) {
                const submods::ModfileValues v = submods::modfile_values(pypath::join(mods_dir(), fn));
                bool dep = false;
                for (const QString &dd : v.dependencies)
                    dep = dep || py::lower(py::strip(dd)) == py::lower(py::strip(mod.name()));
                if (dep)
                    here << other_or(v, u"name"_s, fn.chopped(4));
            }
        state->setText(u"<b>"_s + mod.name() + u"</b> is a mod of its own (no submod)."_s +
                       (!here.isEmpty() ? u"<br>Its submods in this mod folder: <b>"_s + here.join(u", "_s) + u"</b>."_s
                                        : u"<br>It has no submods in this mod folder yet."_s));
    }
    if (py::strip(folder->text()).isEmpty())
        folder->setText(known_4k().value_or(pypath::basename(real_root()) + u" 4K"_s));
    _show();
    _screen_read();
}

// ------------------------------------------------------------------ the 4K screen (C++ only)
fourk::Sizes SubmodWorkspace::sizes() const
{
    fourk::Sizes s;
    s.topbar = WIN_STEPS[size_t(sz_top->value())];
    s.unitpanel = WIN_STEPS[size_t(sz_unit->value())];
    s.marks = MARK_STEPS[size_t(sz_marks->value())];
    s.names = NAME_STEPS[size_t(sz_names->value())];
    return s;
}

void SubmodWorkspace::set_sizes(const fourk::Sizes &s)
{
    const std::pair<StepSlider *, int> to[4] = {{sz_top, nearest(WIN_STEPS, s.topbar)},
                                                {sz_unit, nearest(WIN_STEPS, s.unitpanel)},
                                                {sz_marks, nearest(MARK_STEPS, s.marks)},
                                                {sz_names, nearest(NAME_STEPS, s.names)}};
    for (const auto &[sl, v] : to) {
        sl->blockSignals(true);
        sl->setValue(v);
        sl->blockSignals(false);
    }
    _screen();
}

void SubmodWorkspace::_screen_read()
{
    scriptstore::ScriptStore *store =
        doc && doc->maps.contains(u"scripts"_s) ? &doc->map<scriptstore::ScriptStore>(u"scripts"_s) : nullptr;
    // the sliders start at the sizes the mod's 4K submod was made with (once per submod)
    if (const auto k4 = known_4k(); k4 && *k4 != _sizes_of) {
        _sizes_of = *k4;
        QFile f(pypath::join(mods_dir(), {*k4, fourk::REPORT}));
        if (f.open(QIODevice::ReadOnly))
            set_sizes(fourk::read_sizes(QString::fromUtf8(f.readAll())));
    }
    if (!md || !store)
        return;
    if (!_gw || _gw->version != md->version || _gw->W != md->w) {
        const bool fresh = !_gw;
        _gw = gameview::make_world(mod, *md, [this](long long pid) -> std::optional<QString> {
            return doc->prov_state(pid).owner;
        });
        screen->md = md;
        screen->world = _gw;
        if (fresh) {
            const QPointF at = gameview::start_point(*_gw, mod, *md);
            screen->set_view(at.x(), at.y(), gamecam::START_H);
        }
        screen->player = gameview::owner_at(*_gw, *md, screen->cam.x, screen->cam.z + 30.0);
    }
    // (2026-11-10: read again only when the document, its undo steps or a script text changed — ~30 ms a visit before)
    QStringList texts;
    {
        QStringList rels;
        for (const auto &[r, _d] : store->docs)
            rels << r;
        std::sort(rels.begin(), rels.end());
        for (const QString &r : rels)
            texts << r + u'\x01' + QString::number((*store->docs.get(r))->revision());
    }
    const QString key = u"%1|%2|%3|%4|%5"_s.arg(QString::number(reinterpret_cast<quintptr>(doc)),
                                                QString::number(doc->undo_stack.size()),
                                                QString::number(doc->redo_stack.size()),
                                                QString::number(doc->pending_count()), texts.join(u'\x02'));
    if (_look && key == _look_key) {
        _screen();
        return;
    }
    _look_key = key;
    _look = std::make_unique<locwin::GameLook>(mod, store);   // (the windows as the mod has them now)
    _wins.clear();
    const std::tuple<QString, QString, int> want[3] = {{u"interface/topbar.gui"_s, u"topbar"_s, 0},
                                                       {u"interface/menubar.gui"_s, u"menubar"_s, 2},
                                                       {u"interface/outliner.gui"_s, u"outliner"_s, 2}};
    for (const auto &[rel, win, which] : want)
        for (gameview::Overlay &o : mapmarkspage::screen_windows(*_look, {{rel, win, 1.0}}))
            _wins.emplace_back(which, std::move(o));
    // a picked army (seen in the game 2026-11-09): the head (unitpanel) where single_unitpanel.gui's window stands
    // (x 0, y 150), the row of speed, recon … (sup_unit_status, its background 18 px above its origin) right below it;
    // without the "Select" tab (several armies picked) and the red disband button (an army too small)
    const QString single = u"interface/single_unitpanel.gui"_s;
    double y = 150;
    for (gameview::Overlay &o : mapmarkspage::screen_windows(
             *_look, {{u"interface/unitpanel.gui"_s, u"unitpanel"_s, 1.0, QPointF(0, y),
                       {u"only_unit_from_selection_button"_s, u"disbandtoosmallbutton"_s}}})) {
        y = o.y + o.img.height();
        _wins.emplace_back(1, std::move(o));
    }
    for (gameview::Overlay &o :
         mapmarkspage::screen_windows(*_look, {{single, u"sup_unit_status"_s, 1.0, QPointF(0, y + 18)}}))
        _wins.emplace_back(1, std::move(o));
    const auto parts = mapmarkspage::counter_parts(mod, store);
    _parts = parts ? std::make_shared<mapmarkspage::Parts>(*parts) : nullptr;
    _screen();
}

void SubmodWorkspace::_screen()
{
    const fourk::Sizes s = sizes();
    std::vector<gameview::Overlay> wins;
    for (const auto &[which, o] : _wins) {
        gameview::Overlay w = o;
        w.factor = which == 0 ? s.topbar : which == 1 ? s.unitpanel : double(fourk::S);
        wins.push_back(std::move(w));
    }
    screen->windows = std::move(wins);
    screen->counter_factor = s.marks;
    screen->names_factor = s.names;
    if (_parts) {
        const auto parts = _parts;
        screen->counter = [parts](QPainter &p) { mapmarkspage::draw_counter(p, *parts); };
    } else {
        screen->counter = nullptr;
    }
    screen->update();
    auto x = [](double v) { return std::abs(v - 4.0 / 3.0) < 1e-9 ? u"1⅓"_s : QString::number(v, 'g', 4); };
    sz_note->setText(u"“Make the 4K submod” makes: top bar × %1, unit panel × %2, army counters × %3, province names "
                     u"× %4, everything else × 2. (× 2 is as big as on a 1920 × 1080 screen.)"_s
                         .arg(x(s.topbar), x(s.unitpanel), x(s.marks), x(s.names)));
}

std::optional<QString> SubmodWorkspace::known_4k() const
{
    const QString me = py::lower(py::strip(chain_names().last()));
    bool ok = false;
    const QStringList names = sorted_names(mods_dir(), &ok);
    if (!ok)
        return std::nullopt;
    for (const QString &fn : names)
        if (py::lower(fn).endsWith(u".mod")) {
            const submods::ModfileValues v = submods::modfile_values(pypath::join(mods_dir(), fn));
            const QString fd = folder_of(v);
            bool dep = false;
            for (const QString &dd : v.dependencies)
                dep = dep || py::lower(py::strip(dd)) == me;
            if (py::lower(fd).endsWith(u"4k") && dep && QFileInfo(pypath::join(mods_dir(), fd)).isDir())
                return fd;
        }
    return std::nullopt;
}

std::optional<QString> SubmodWorkspace::target() const
{
    const QString name = py::strip(folder->text());
    if (name.isEmpty() || !submods::name_ok(name) || name.endsWith(u'.') || name.endsWith(u' '))
        return std::nullopt;
    return pypath::join(mods_dir(), name);
}

void SubmodWorkspace::_show()
{
    if (_running)
        return;
    std::optional<QString> t = target();
    QStringList notes;
    if (!t) {
        where->setText(QString());
        notes << u"⚠ A folder name is 1–80 characters without \\ / : * ? \" < > |"_s;
    } else {
        where->setText(*t + u"  +  "_s + pypath::basename(*t) + u".mod — the game loads it after: "_s +
                       chain_names().join(u", "_s));
        bool own = pypath::normcase(*t) == pypath::normcase(real_root());
        if (mod.submod)
            for (const QString &r : mod.submod->parent_roots)
                own = own || pypath::normcase(*t) == pypath::normcase(r);
        if (own) {
            notes << u"⚠ That is this mod's own folder — the submod needs one of its own."_s;
            t.reset();
        } else if (nonempty_dir(*t)) {
            notes << u"That folder is there already: its interface files and pictures are made anew (click twice)."_s;
        }
    }
    if (doc && doc->pending_count())
        notes << u"There are pending edits: the submod is made from the files as they are on disk — Apply first if "
                 u"they should be in it."_s;
    make_btn->setEnabled(t.has_value());
    warn->setText(notes.join(u"\n"_s));
    warn->setVisible(!notes.isEmpty());
    _show_ck();
}

// ------------------------------------------------------------------ the checksum check
std::map<QString, SubmodWorkspace::Entry> SubmodWorkspace::_entries() const
{
    std::map<QString, Entry> out;
    bool ok = false;
    const QStringList names = sorted_names(mods_dir(), &ok);
    for (const QString &fn : names)
        if (py::lower(fn).endsWith(u".mod")) {
            const submods::ModfileValues v = submods::modfile_values(pypath::join(mods_dir(), fn));
            QString fd = folder_of(v);
            if (fd.isEmpty())
                fd = fn.chopped(4);
            const QString key = py::lower(py::strip(other_or(v, u"name"_s, fn.chopped(4))));
            const QString *ud = v.other.get(u"user_dir"_s);
            out.emplace(key, Entry{fn, ud ? std::optional<QString>(*ud) : std::nullopt, pypath::join(mods_dir(), fd),
                                   v.replace_path});   // (setdefault: the first one stays)
        }
    return out;
}

std::vector<std::pair<QString, std::vector<SubmodWorkspace::Entry>>> SubmodWorkspace::ck_sets() const
{
    const QStringList names = chain_names();
    const std::map<QString, Entry> ents = _entries();
    auto of = [&ents](const QStringList &ns) -> std::optional<std::vector<Entry>> {
        std::vector<Entry> rows;
        for (const QString &n : ns) {
            const auto it = ents.find(py::lower(py::strip(n)));
            if (it == ents.end())
                return std::nullopt;
            rows.push_back(it->second);
        }
        if (rows.empty())
            return std::nullopt;
        return rows;
    };
    std::vector<std::pair<QString, std::optional<std::vector<Entry>>>> sets;
    if (mod.submod) {
        sets.emplace_back(u"the parent alone"_s, of(names.mid(0, names.size() - 1)));
        sets.emplace_back(u"with this submod"_s, of(names));
    } else {
        sets.emplace_back(u"the mod alone"_s, of(names));
    }
    const std::optional<QString> t = target();
    if (t && QFileInfo(*t + u".mod"_s).isFile() && of(names) && pypath::normcase(*t) != pypath::normcase(real_root())) {
        const submods::ModfileValues v = submods::modfile_values(*t + u".mod"_s);
        std::vector<Entry> rows = *of(names);
        const QString *ud = v.other.get(u"user_dir"_s);
        rows.push_back(Entry{pypath::basename(*t) + u".mod"_s, ud ? std::optional<QString>(*ud) : std::nullopt, *t,
                             v.replace_path});
        sets.emplace_back(u"with "_s + pypath::basename(*t), rows);
    }
    std::vector<std::pair<QString, std::vector<Entry>>> out;
    for (const auto &[what, rows] : sets)
        if (rows)
            out.emplace_back(what, *rows);
    return out;
}

void SubmodWorkspace::_show_ck()
{
    if (_probe)
        return;
    QStringList parts;
    std::vector<std::pair<QString, QString>> roots;
    if (mod.submod)
        roots.emplace_back(u"This submod"_s, mod.submod->root);
    const std::optional<QString> t = target();
    if (t && QFileInfo(*t).isDir())
        roots.emplace_back(u"The 4K submod “"_s + pypath::basename(*t) + u"”"_s, *t);
    for (const auto &[what, root] : roots) {
        const auto files = submods::holds(root);
        QStringList bad;
        for (const auto &fl : files)
            if (std::get<1>(fl))
                bad << std::get<0>(fl);
        if (!bad.isEmpty())
            parts << u"<b>"_s + what + u"</b>: "_s + QString::number(qsizetype(files.size())) +
                         u" files, <span style='color:"_s + theme::WARN + u"; font-weight:600'>"_s +
                         QString::number(bad.size()) + u" in folders that count — it changes the checksum</span>: "_s +
                         bad.mid(0, 12).join(u", "_s) + (bad.size() > 12 ? u" …"_s : QString());
        else
            parts << u"<b>"_s + what + u"</b>: "_s + QString::number(qsizetype(files.size())) +
                         u" files, <span style='color:"_s + theme::OK +
                         u"; font-weight:600'>none in a folder that counts — the checksum stays</span> (both ticked in "
                         u"the launcher)."_s;
    }
    ck_files->setText(parts.join(u"<br>"_s));
    ck_files->setVisible(!parts.isEmpty());
    const bool ok = !ck_sets().empty() && mod.base && !mod.base->isEmpty();
    ck_btn->setEnabled(ok);
    code_btn->setEnabled(ok);
}

checksum::Prediction SubmodWorkspace::_code_of(const std::vector<Entry> &rows) const
{
    // C++ only (2026-11-09): the game's own checksum, worked out as the game does (map/checksum.h)
    const QString game = mod.base.value_or(QString());
    std::vector<checksum::ModIn> mods;
    for (const Entry &row : rows) {
        QString mf = pypath::join(mods_dir(), std::get<0>(row));
        if (!QFileInfo(mf).isFile())
            mf = std::get<2>(row) + u".mod"_s;
        checksum::ModIn m = checksum::mod_in(game, mf);
        m.dir = QDir::cleanPath(std::get<2>(row));    // (the folder the editor found, even outside the game)
        mods.push_back(m);
    }
    return checksum::predict(game, mods);
}

void SubmodWorkspace::_codes()
{
    const QString b = u"<b style='font-family:"_s + theme::MONO + u"'>"_s;
    const QString faint = u"<span style='color:"_s + theme::INK_FAINT + u"'>"_s;
    QStringList lines, notes;
    for (const auto &[what, rows] : ck_sets()) {
        const checksum::Prediction p = _code_of(rows);
        if (!p.ok) {
            lines << cap(what) + u": <span style='color:"_s + theme::WARN + u"'>⚠ "_s + p.why + u"</span>"_s;
            continue;
        }
        lines << cap(what) + u": "_s + b + checksum::letters(p.steady) + u"</b> "_s + faint + u"("_s +
                     QString::number(p.files) + u" files counted)</span>"_s;
        if (p.next != p.steady)
            lines << u"&nbsp;&nbsp;&nbsp;the next start on this computer shows "_s + b + checksum::letters(p.next) +
                         u"</b> once: "_s + p.next_why;
        for (const QString &n : p.notes)
            if (!notes.contains(n))
                notes << n;
    }
    lines << faint + u"The game's own letters, worked out the way the game does it (read from the game's code and "
                     u"checked against the game): the same on every computer with these mods, from the moment the game "
                     u"has been started with them once. "_s + notes.join(u' ') + u"</span>"_s;
    ck_result->setText(lines.join(u"<br>"_s));
}

void SubmodWorkspace::_measure()
{
    if (_probe)
        return;
    const QString game = mod.base.value_or(QString());
    if (pypath::normcase(pypath::abspath(mods_dir())) != pypath::normcase(pypath::join(game, u"mod"_s))) {
        ck_result->setText(u"<span style='color:"_s + theme::ERROR_ +
                           u"'>⚠ The game only loads mods from its own mod folder ("_s + pypath::join(game, u"mod"_s) +
                           u") — this mod is somewhere else.</span>"_s);
        return;
    }
    _todo = ck_sets();
    _done.clear();
    ck_btn->setEnabled(false);
    code_btn->setEnabled(false);
    ck_stop->show();
    if (!_quit_hooked) {
        _quit_hooked = true;
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { _measure_stop(); });
    }
    _next_probe();
}

void SubmodWorkspace::_next_probe()
{
    if (_todo.empty()) {
        _probe.reset();
        _ck_timer->stop();
        ck_stop->hide();
        _ck_report();
        _show_ck();
        return;
    }
    const auto [what, rows] = _todo.front();
    QStringList mfs, uds;
    for (const Entry &r : rows) {
        mfs << std::get<0>(r);
        uds << std::get<1>(r).value_or(QString());
    }
    _probe = std::make_unique<checksum::Probe>(mod.base.value_or(QString()), mfs, uds);
    try {
        _probe->start();
    } catch (const std::exception &e) {
        _done.push_back({what, {u"error"_s, u"the game could not be started: "_s + QString::fromUtf8(e.what())},
                         QString()});
        _todo.erase(_todo.begin());
        _probe->finish();
        _next_probe();
        return;
    }
    _ck_report(what);
    _ck_timer->start();
}

void SubmodWorkspace::_measure_tick()
{
    if (!_probe) {
        _ck_timer->stop();
        return;
    }
    const std::optional<checksum::Result> r = _probe->poll();
    if (!r) {
        _ck_report(_todo.front().first);
        return;
    }
    const auto [what, rows] = _todo.front();
    _todo.erase(_todo.begin());
    const checksum::Prediction p = _code_of(rows);   // (the probe starts in a new settings folder: compare with that)
    _done.push_back({what, *r, p.ok ? checksum::letters(p.fresh) : QString()});
    _next_probe();
}

void SubmodWorkspace::_measure_stop()
{
    if (_probe) {
        _probe->finish();
        _todo.clear();
        _next_probe();
    }
}

void SubmodWorkspace::_ck_report(const std::optional<QString> &running)
{
    QStringList lines;
    for (const Done &dn : _done) {
        if (dn.r.first == u"ok")
            lines << cap(dn.what) + u": <b style='font-family:"_s + theme::MONO + u"'>"_s + dn.r.second +
                         u"</b> <span style='color:"_s + theme::INK_FAINT + u"'>(worked out for a new settings folder: "_s +
                         (dn.code.isEmpty() ? u"—"_s : dn.code + (dn.code == dn.r.second ? u" ✓"_s : u" ✗"_s)) +
                         u")</span>"_s;
        else
            lines << cap(dn.what) + u": <span style='color:"_s + theme::ERROR_ + u"'>⚠ "_s + dn.r.second +
                         u"</span>"_s;
    }
    if (running) {
        const long long secs = _probe && _probe->t0 ? (long long)(checksum::monotonic() - _probe->t0) : 0;
        lines << u"<span style='color:"_s + theme::INK_FAINT + u"'>"_s + cap(*running) +
                     u": the game is loading (minimised) … "_s + QString::number(secs) + u" s</span>"_s;
    } else {
        QStringList got;
        for (const Done &dn : _done)
            if (dn.r.first == u"ok")
                got << dn.r.second;
        if (got.size() >= 2 && got.size() == qsizetype(_done.size())) {
            const std::set<QString> uniq(got.begin(), got.end());
            if (uniq.size() == 1)
                lines << u"<span style='color:"_s + theme::OK +
                             u"; font-weight:600'>The same letters: players with and without the submod can play "
                             u"together.</span>"_s;
            else
                lines << u"<span style='color:"_s + theme::WARN +
                             u"; font-weight:600'>Different letters: everyone needs the same mods ticked to play "
                             u"together.</span>"_s;
        }
        if (!_done.empty())
            lines << u"<span style='color:"_s + theme::INK_FAINT + u"'>Measured "_s + checksum::stamp() +
                         u", each in a fresh settings folder of its own (taken into the Recycle Bin afterwards).</span>"_s;
    }
    ck_result->setText(lines.join(u"<br>"_s));
}

// ------------------------------------------------------------------ making it
void SubmodWorkspace::_ask_stop()
{
    _stop = true;
    progress->setText(u"Stopping after this file…"_s);
}

void SubmodWorkspace::_make()
{
    const std::optional<QString> t = target();
    if (!t || _running)
        return;
    if (nonempty_dir(*t) && checksum::monotonic() - _armed > 10) {
        _armed = checksum::monotonic();            // no pop-up: the second click within 10 s does it
        make_btn->setText(u"Make it anew — click again"_s);
        return;
    }
    _armed = 0.0;
    make_btn->setText(u"Make the 4K submod"_s);
    run(*t);
}

void SubmodWorkspace::run(const QString &tgt)
{
    _running = true;
    _stop = false;
    make_btn->setEnabled(false);
    folder->setEnabled(false);
    stop_btn->show();
    bar->show();
    result->setText(QString());
    const double t0 = checksum::monotonic();
    double last = 0.0;
    auto tick = [&](long long done, long long total, const QString &what) {
        if (_stop)
            throw fourk::Cancelled();
        const double now = checksum::monotonic();
        if (now - last > 0.08 || done == total) {
            last = now;
            bar->setMaximum(int(std::max(1LL, total)));
            bar->setValue(int(done));
            progress->setText(QString::number(done) + u" of "_s + QString::number(total) + u" — "_s + what);
            QApplication::processEvents();
        }
    };
    std::optional<fourk::Report> rep;
    QString err, modfile;
    try {
        rep = fourk::build(mod, tgt, tick, 0, sizes());   // (C++: the 4K screen's sizes)
        const QString name = pypath::basename(tgt);
        modfile = pypath::join(pypath::dirname(tgt), name + u".mod"_s);
        if (!QFileInfo(modfile).isFile())
            submods::write_modfile(modfile, chain_names().last() + u" - 4K interface (tick both)"_s, name,
                                   chain_names(), name);
    } catch (const fourk::Cancelled &) {
        rep.reset();
        err = u"Stopped. What was written so far is in "_s + tgt + u"; making it again goes over it."_s;
    } catch (const std::exception &e) {           // (a full disk, a folder that cannot be written …)
        rep.reset();
        err = u"⚠ The 4K submod could not be made: "_s + QString::fromUtf8(e.what());
    }
    _running = false;
    stop_btn->hide();
    bar->hide();
    progress->setText(QString());
    folder->setEnabled(true);
    if (!rep) {
        result->setText(u"<span style='color:"_s + theme::ERROR_ + u"'>"_s + err + u"</span>"_s);
        Q_EMIT message(err);
        _show();
        return;
    }
    const double secs = checksum::monotonic() - t0;
    auto n = [](long long count, const QString &one, const QString &many = {}) {
        return QString::number(count) + u' ' + (count == 1 ? one : (!many.isEmpty() ? many : one + u"s"_s));
    };
    const fourk::Report &r = *rep;
    QStringList lines;
    lines << u"<b>Made in "_s + QString::number((long long)secs) + u" s:</b> "_s + n(r.scripts, u"interface file"_s) +
                 u" ("_s + n(r.numbers, u"number"_s) + u" doubled), "_s + n(r.pictures, u"picture"_s) + u", "_s +
                 n(r.fonts, u"font"_s) + u", "_s + n(r.cursors, u"mouse pointer"_s) + u" — "_s + n(r.files, u"file"_s) +
                 u", "_s + QString::number(double(r.bytes) / std::pow(2.0, 30), 'f', 2) + u" GB in<br>"_s + tgt;
    lines << u"In the game's launcher tick: <b>"_s + chain_names().join(u"</b>, <b>"_s) + u"</b> and <b>"_s +
                 other_or(submods::modfile_values(modfile), u"name"_s, pypath::basename(tgt)) +
                 u"</b>. The pair has its own settings and saved games (Documents › Paradox Interactive › Victoria II); "
                 u"without settings of its own the game starts borderless at the size of your screen."_s;
    if (!r.problems.isEmpty())
        lines << u"<span style='color:"_s + theme::ERROR_ + u"'><b>"_s + QString::number(r.problems.size()) +
                     u" files could not be made:</b><br>"_s + r.problems.mid(0, 30).join(u"<br>"_s) + u"</span>"_s;
    if (!r.gone.isEmpty())
        lines << u"Made by the last build and no longer by this one — taken out: "_s + r.gone.mid(0, 20).join(u", "_s);
    lines << u"<span style='color:"_s + theme::INK_FAINT + u"'>The list of everything is in “"_s + fourk::REPORT +
                 u"” in that folder.</span>"_s;
    QStringList special;
    for (const QString &x : r.notes)
        if (!x.contains(u"DXT1 with"_s))
            special << x;
    if (!special.isEmpty())
        lines << u"<span style='color:"_s + theme::INK_FAINT + u"'>Written differently than they were ("_s +
                     QString::number(special.size()) + u"): "_s + special.mid(0, 12).join(u"; "_s) + u"</span>"_s;
    result->setText(lines.join(u"<br><br>"_s));
    Q_EMIT message((!r.problems.isEmpty() ? u"⚠ "_s : QString()) + u"4K submod made in "_s + tgt + u" ("_s +
                   QString::number(r.files) + u" files"_s +
                   (!r.problems.isEmpty() ? u", "_s + QString::number(r.problems.size()) + u" could not be made"_s
                                          : QString()) +
                   u")."_s);
    _show();
}

}  // namespace submodpage
