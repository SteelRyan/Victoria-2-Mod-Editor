// app.py — line by line.
#include "app/app.h"
#include "app/version.h"
#include "app/update.h"
#include "ui/drawtext.h"
#include "app/mainwindow.h"
#include "core/fs.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "core/settings.h"
#include "core/modcopy.h"
#include "core/modfolders.h"
#include "core/submods.h"
#include "map/mapview_data.h"
#include "ui/theme.h"

#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QBrush>
#include <QCheckBox>
#include <QDropEvent>
#include <QTimer>
#include <QComboBox>
#include <QDesktopServices>
#include <QFrame>
#include <QUrl>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFontMetrics>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHash>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;

namespace app {

const QStringList STEAM_MOD_DIRS = {
    uR"(C:\Program Files (x86)\Steam\steamapps\common\Victoria 2\mod)"_s,
    uR"(C:\Program Files\Steam\steamapps\common\Victoria 2\mod)"_s,
    uR"(C:\Program Files (x86)\Paradox Interactive\Victoria 2\mod)"_s,
    uR"(C:\GOG Games\Victoria 2\mod)"_s,
};

static QString err_text(const std::exception &e) { return QString::fromUtf8(e.what()); }   // str(e)

static double monotonic()                     // time.monotonic()
{
    static QElapsedTimer t = [] {
        QElapsedTimer x;
        x.start();
        return x;
    }();
    return t.nsecsElapsed() / 1e9;
}

// re.sub(r"[^\w.\- ]", "_", name)
static QString safe_name(const QString &name)
{
    QString out = name;
    for (QChar &c : out)
        if (!py::is_word_char(c) && c != u'.' && c != u'-' && c != u' ')
            c = u'_';
    return out;
}

QString app_dir() { return pypath::normpath(QCoreApplication::applicationDirPath()); }

bool has_mods(const QString &path)
{
    return !path.isEmpty() && fs::isdir(path) && !moddata::discover_mods(path).first.empty();
}

QString guess_mods_dir(QSettings &settings)
{
    const QString saved = settings.value(u"modsDir"_s, u""_s).toString();
    if (has_mods(saved))
        return saved;
    const QString here = app_dir();
    QStringList cands = {here, pypath::dirname(here), pypath::join(pypath::dirname(here), u"mod"_s)};
    cands += STEAM_MOD_DIRS;
    for (const QString &cand : cands)
        if (has_mods(cand))
            return cand;
    return u""_s;
}

QIcon make_icon()
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(theme::PANEL));
    p.setPen(QColor(theme::GOLD));
    p.drawRoundedRect(2, 2, 60, 60, 10, 10);
    p.setFont(QFont(u"Segoe UI Symbol"_s, 30));
    drawtext::draw(p, pm.rect(), Qt::AlignCenter, u"★"_s);
    p.end();
    return QIcon(pm);
}

// ---------------------------------------------------------------------------------------------- NoWheelFilter
bool NoWheelFilter::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() == QEvent::Wheel &&
        (qobject_cast<QComboBox *>(obj) || qobject_cast<QAbstractSpinBox *>(obj) || qobject_cast<QSlider *>(obj))) {
        QWidget *w = static_cast<QWidget *>(obj)->parentWidget();
        while (w && !qobject_cast<QAbstractScrollArea *>(w))
            w = w->parentWidget();
        if (w)
            QApplication::sendEvent(static_cast<QAbstractScrollArea *>(w)->viewport(), ev);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------- Splash
Splash::Splash() : QWidget(nullptr, Qt::SplashScreen | Qt::FramelessWindowHint)
{
    setStyleSheet(u"background:%1; border:1px solid %2;"_s.arg(theme::PANEL, theme::LINE));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(30, 24, 30, 24);
    auto *t = new QLabel(u"ModEditor"_s);
    t->setStyleSheet(u"font-size:20px; font-weight:600; color:%1; border:none;"_s.arg(theme::GOLD));
    msg = new QLabel(u"Starting…"_s);
    msg->setStyleSheet(u"color:%1; border:none;"_s.arg(theme::INK_DIM));
    lay->addWidget(t);
    lay->addWidget(msg);
    resize(420, 110);
}

void Splash::say(const QString &text)
{
    msg->setText(text);
    QApplication::processEvents();
}

// ---------------------------------------------------------------------------------------------- OpenDialog
static const QString SC_HINT = u"Each folder in history/pops is a separate scenario. Edits only affect the folder you pick."_s;

OpenDialog::OpenDialog(QSettings &settings_, const QString &mods_dir_, const OptStr &error, QWidget *parent)
    : QDialog(parent), settings(settings_), mods_dir(mods_dir_)
{
    base_set = settings.value(u"baseGame"_s, u""_s).toString();          // (C++ only)
    more_dirs = settings.value(u"moreModDirs"_s).toStringList();
    setWindowTitle(u"ModEditor " MODEDITOR_VERSION_TEXT " — open mod"_s);
    resize(940, 520);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(18, 16, 18, 16);
    lay->setSpacing(8);

    // C++ only (the user 2026-11-15): "a new version is out" (update.h) — a strip, never a pop-up. Its button opens
    // the releases page built into the program; the editor itself downloads and runs nothing.
    upd_strip = new QFrame();
    upd_strip->setObjectName(u"updstrip"_s);
    upd_strip->setStyleSheet(u"QFrame#updstrip { border: 1px solid %1; border-radius: 6px; background: %2; }"_s.arg(
        theme::GOLD, theme::PANEL_ALT));
    auto *us = new QHBoxLayout(upd_strip);
    us->setContentsMargins(12, 6, 8, 6);
    upd_label = new QLabel();
    upd_label->setTextFormat(Qt::PlainText);
    upd_label->setStyleSheet(u"color: %1; font-weight: 600;"_s.arg(theme::GOLD));
    upd_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    upd_label->setWordWrap(true);
    us->addWidget(upd_label, 1);
    auto *get = new QPushButton(u"Open the download page"_s);
    get->setToolTip(update::RELEASES_PAGE);
    connect(get, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(update::RELEASES_PAGE)); });
    us->addWidget(get);
    auto *hide = new QPushButton(u"✕"_s);
    hide->setToolTip(u"Don't show this version again (a newer one will show)"_s);
    hide->setFixedWidth(34);
    connect(hide, &QPushButton::clicked, this, [] {
        if (auto *c = update::Checker::instance())
            c->hide_this_one();
    });
    us->addWidget(hide);
    upd_strip->hide();
    lay->addWidget(upd_strip);

    auto *row = new QHBoxLayout();
    row->addWidget(_lab(u"Victoria 2 mod folder:"_s, u"sub"_s));
    dir_label = _lab(u""_s, u"title"_s);
    dir_label->setStyleSheet(u"font-size:12px;"_s);
    dir_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(dir_label, 1);
    auto *change = new QPushButton(u"Change…"_s);
    connect(change, &QPushButton::clicked, this, [this] { choose_dir(); });
    row->addWidget(change);
    add_dir_btn = new QPushButton(u"＋ Another mod folder…"_s);   // (C++ only)
    add_dir_btn->setToolTip(u"Mods kept somewhere else (outside the game, e.g. for work): their folder is listed too, "
                            u"below a line \"All below are from …\".\nThe editor edits them where they are; the game "
                            u"plays only the mods in its own mod folder — drag one there to play it (Ctrl+drag: a copy)."_s);
    connect(add_dir_btn, &QPushButton::clicked, this, [this] { add_dir(); });
    row->addWidget(add_dir_btn);
    upd_box = new QCheckBox(u"Tell me as soon as a new version of ModEditor is out"_s);
    upd_box->setToolTip(u"Once a day the editor asks GitHub (SteelRyan/Victoria-2-Mod-Editor) which version is the "
                        u"newest and shows a note here.\nIt never downloads or runs anything. Untick: no asking, "
                        u"no note."_s);
    if (auto *c = update::Checker::instance()) {
        upd_box->setChecked(c->enabled());
        connect(upd_box, &QCheckBox::toggled, this, [](bool on) {
            if (auto *ck = update::Checker::instance())
                ck->set_enabled(on);
        });
        connect(c, &update::Checker::changed, this, [this] { show_update(); });
    } else {
        upd_box->hide();                       // (the check programs: no Checker, nothing asked)
    }
    row->addWidget(upd_box);
    lay->addLayout(row);
    row = new QHBoxLayout();                     // (C++ only) the base game: found, or a path of its own
    row->addWidget(_lab(u"Base game:"_s, u"sub"_s));
    game_label = _lab(u""_s, u"sub"_s);
    game_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(game_label, 1);
    game_btn = new QPushButton(u"Change…"_s);
    game_btn->setToolTip(u"The Victoria 2 folder (with v2game.exe, map, common …): what a mod does not have itself "
                         u"comes from there.\nNeeded when the mod folders are not inside the game's folder."_s);
    connect(game_btn, &QPushButton::clicked, this, [this] { choose_base(); });
    row->addWidget(game_btn);
    game_auto = new QPushButton(u"✕"_s);
    game_auto->setFixedWidth(34);
    game_auto->setToolTip(u"Forget this path: the base game is looked for next to the mod folder again"_s);
    connect(game_auto, &QPushButton::clicked, this, [this] {
        base_set.clear();
        settings.setValue(u"baseGame"_s, base_set);
        fill_mods();
    });
    row->addWidget(game_auto);
    lay->addLayout(row);

    auto *cols = new QHBoxLayout();
    auto *left = new QVBoxLayout();
    left->addWidget(_lab(u"1.  Which mod do you want to edit?"_s, u"title"_s));
    mod_list = new QListWidget();
    connect(mod_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *cur) { mod_changed(cur); });
    mod_list->viewport()->installEventFilter(this);   // (a click on a "Safeguards" row folds it open / shut)
    mod_list->setDragEnabled(true);              // (C++ only) a mod dragged to another folder / the safeguards
    mod_list->setAcceptDrops(true);
    mod_list->viewport()->setAcceptDrops(true);
    mod_list->setDragDropMode(QAbstractItemView::DragDrop);
    mod_list->setDefaultDropAction(Qt::MoveAction);
    mod_list->setToolTip(u"Drag a mod onto another folder's line (or a mod in it), or onto a mod's Safeguards: it moves "
                         u"there with its .mod file.\nCtrl+drag: a copy. A safeguard dragged into a mod folder is a "
                         u"mod of its own again."_s);
    left->addWidget(mod_list, 1);
    drop_note = _lab(u""_s, u"sub"_s);
    drop_note->setStyleSheet(u"color: %1;"_s.arg(theme::GOLD));
    drop_note->setTextInteractionFlags(Qt::TextSelectableByMouse);
    drop_note->hide();
    left->addWidget(drop_note);
    cols->addLayout(left, 1);
    auto *right = new QVBoxLayout();
    right->addWidget(_lab(u"2.  Which scenario (start date)?"_s, u"title"_s));
    sc_hint = _lab(SC_HINT, u"sub"_s);
    right->addWidget(sc_hint);
    sc_list = new QListWidget();
    connect(sc_list, &QListWidget::itemDoubleClicked, this, [this] { try_accept(); });
    right->addWidget(sc_list, 1);
    cols->addLayout(right, 1);
    lay->addLayout(cols, 1);

    // problems are shown here in red (no pop-ups)
    err = _lab(error.value_or(u""_s), u"warn"_s);
    err->setTextInteractionFlags(Qt::TextSelectableByMouse);
    err->setVisible(error && !error->isEmpty());
    lay->addWidget(err);

    row = new QHBoxLayout();
    new_btn = new QPushButton(u"New scenario (copy of selected)…"_s);
    connect(new_btn, &QPushButton::clicked, this, [this] { new_scenario(); });
    row->addWidget(new_btn);
    sub_btn = new QPushButton(u"New submod of the selected mod…"_s);
    sub_btn->setToolTip(u"A submod holds only what it changes; the game loads it on top of its parent mod.\n"
                        u"Open the new submod and edit: Apply writes into the submod, never into the parent."_s);
    connect(sub_btn, &QPushButton::clicked, this, [this] { new_submod(); });
    row->addWidget(sub_btn);
    // C++ only (the user 2026-11-13): a mod of your own, started from the base game — no other mod needed
    base_btn = new QPushButton(u"New mod from the base game…"_s);
    base_btn->setToolTip(u"A new, empty mod in this mod folder (its own folder and .mod file, no other mod under it).\n"
                         u"In the editor you see the base game; only what you change is saved into the new mod."_s);
    connect(base_btn, &QPushButton::clicked, this, [this] { new_base_mod(); });
    row->addWidget(base_btn);
    del_btn = new QPushButton(u"Delete this submod…"_s);
    del_btn->setToolTip(u"Moves the submod's folder and its .mod file to the Recycle Bin (and the editor's "
                        u"working copy of it). The parent mod is not touched."_s);
    connect(del_btn, &QPushButton::clicked, this, [this] { delete_submod(); });
    del_btn->setEnabled(false);
    row->addWidget(del_btn);
    row->addStretch(1);
    auto *quit_btn = new QPushButton(u"Quit"_s);
    connect(quit_btn, &QPushButton::clicked, this, &QDialog::reject);
    ok_btn = new QPushButton(u"Open"_s);
    ok_btn->setObjectName(u"apply"_s);
    connect(ok_btn, &QPushButton::clicked, this, [this] { try_accept(); });
    row->addWidget(quit_btn);
    row->addWidget(ok_btn);
    lay->addLayout(row);
    fill_mods();
    show_update();
}

void OpenDialog::show_update()
{
    const auto *c = update::Checker::instance();
    const auto n = c ? c->newer() : std::nullopt;
    upd_strip->setVisible(n.has_value());
    if (n)
        upd_label->setText(u"A new version of ModEditor is out: %1 (you have %2). Get it from the releases page."_s.arg(
            *n, QStringLiteral(MODEDITOR_VERSION_TEXT)));
}

QLabel *OpenDialog::_lab(const QString &text, const QString &obj)
{
    auto *l = new QLabel(text);
    l->setObjectName(obj);
    l->setWordWrap(true);
    return l;
}

const ModEntry *OpenDialog::_entry_of(QListWidgetItem *it) const   // (Python: it.data(Qt.UserRole), the mod's dict)
{
    if (!it)
        return nullptr;
    const QVariant v = it->data(Qt::UserRole);
    if (!v.isValid())
        return nullptr;
    const int i = v.toInt();
    return i >= 0 && size_t(i) < mods.size() ? &mods[size_t(i)] : nullptr;
}

void OpenDialog::choose_dir()
{
    QString path = QFileDialog::getExistingDirectory(this, u"Select the Victoria 2 'mod' folder"_s, mods_dir);
    if (path.isEmpty())
        return;
    if (!has_mods(path)) {
        // maybe they picked the game folder or a single mod folder
        if (has_mods(pypath::join(path, u"mod"_s)))
            path = pypath::join(path, u"mod"_s);
        else if (moddata::is_mod_folder(path) || fs::isdir(pypath::join(path, {u"history"_s, u"pops"_s})))
            path = pypath::dirname(path);
        else {
            show_error(u"⚠ No mods (.mod files or mod folders) found in %1"_s.arg(path));
            return;
        }
    }
    show_error(u""_s);
    mods_dir = path;
    fill_mods();
}

void OpenDialog::choose_base()
{
    QString path = QFileDialog::getExistingDirectory(this, u"Select the Victoria 2 game folder"_s,
                                                     base_set.isEmpty() ? base.value_or(mods_dir) : base_set);
    if (path.isEmpty())
        return;
    if (!moddata::is_game_folder(path)) {
        if (moddata::is_game_folder(pypath::dirname(path)))   // (the game's mod folder picked)
            path = pypath::dirname(path);
        else {
            show_error(u"⚠ %1 is not a Victoria 2 folder (no map, common, history … in it)"_s.arg(path));
            return;
        }
    }
    show_error(u""_s);
    base_set = path;
    settings.setValue(u"baseGame"_s, base_set);
    fill_mods();
}

void OpenDialog::add_dir(const QString &given)
{
    QString path = given.isEmpty()
                       ? QFileDialog::getExistingDirectory(this, u"Select a folder that holds mods"_s, mods_dir)
                       : given;
    if (path.isEmpty())
        return;
    if (moddata::is_mod_folder(path) && !has_mods(path))   // (one mod picked: the folder it lies in)
        path = pypath::dirname(path);
    path = pypath::abspath(path);
    for (const QString &d : QStringList{mods_dir} + more_dirs)
        if (!d.isEmpty() && pypath::normcase(pypath::abspath(d)) == pypath::normcase(path)) {
            show_error(u"⚠ %1 is in the list already"_s.arg(path));
            return;
        }
    show_error(u""_s);
    more_dirs << path;
    settings.setValue(u"moreModDirs"_s, more_dirs);
    if (mods_dir.isEmpty()) {                    // (no first folder yet: this one is it)
        mods_dir = more_dirs.takeLast();
        settings.setValue(u"moreModDirs"_s, more_dirs);
    }
    fill_mods();
}

void OpenDialog::remove_dir(const QString &path)
{
    for (qsizetype i = 0; i < more_dirs.size(); ++i)
        if (pypath::normcase(pypath::abspath(more_dirs[i])) == pypath::normcase(pypath::abspath(path))) {
            more_dirs.removeAt(i);
            settings.setValue(u"moreModDirs"_s, more_dirs);
            drop_note->setText(u"Taken off the list (nothing deleted): %1"_s.arg(path));
            drop_note->show();
            fill_mods();
            return;
        }
}

// rows of the list: a folder's line carries its path in SEP_DIR; every mod row (and a folder's "no mods" row) the
// folder it is listed under in GROUP_DIR
static constexpr int SEP_DIR = Qt::UserRole + 3, GROUP_DIR = Qt::UserRole + 4, SEP_GAME = Qt::UserRole + 5;

void OpenDialog::_sep_texts()
{
    // "━━  All below are from:  C:\…\Mods\Work" — the middle of a long path left out, so the line fits (no
    // horizontal scroll bar) and the end of the path shows
    const QFontMetrics fm(mod_list->font());
    const int room = mod_list->viewport()->width() - 12;
    for (int r = 0; r < mod_list->count(); ++r) {
        QListWidgetItem *it = mod_list->item(r);
        if (const QVariant d = it->data(SEP_DIR); d.isValid()) {
            const QString head = u"━━  All below are from:  "_s,
                          tail = it->data(SEP_GAME).toBool() ? u"   · the game's mod folder"_s : QString();
            const int w = std::max(60, room - fm.horizontalAdvance(head + tail));
            it->setText(head + fm.elidedText(d.toString(), Qt::ElideMiddle, w) + tail);
        }
    }
}

QString OpenDialog::target_dir(QListWidgetItem *t) const
{
    if (!t) {                                    // (below the last row: the last folder)
        for (int r = mod_list->count() - 1; r >= 0; --r)
            if (const QVariant g = mod_list->item(r)->data(GROUP_DIR); g.isValid())
                return g.toString();
        return mods_dir;
    }
    if (const QVariant d = t->data(SEP_DIR); d.isValid())
        return d.toString();
    if (t->data(Qt::UserRole + 1).isValid()) {   // a "Safeguards" row: the mod above it
        for (int r = mod_list->row(t) - 1; r >= 0; --r)
            if (const ModEntry *m = _entry_of(mod_list->item(r)); m && m->folder)
                return pypath::join(pypath::abspath(*m->folder), modcopy::SAFE_DIR);
        return QString();
    }
    if (t->data(Qt::UserRole + 2).isValid())     // a safeguard: its mod's safeguards
        if (const ModEntry *m = _entry_of(t); m && m->folder)
            return pypath::dirname(pypath::abspath(*m->folder));
    if (const QVariant g = t->data(GROUP_DIR); g.isValid())
        return g.toString();
    return QString();
}

bool OpenDialog::drop_on(QListWidgetItem *src, QListWidgetItem *target, bool copy)
{
    const ModEntry *mp = _entry_of(src);
    const QString to = target_dir(target);
    if (!mp || to.isEmpty())
        return false;
    const ModEntry m = *mp;
    const auto r = modfolders::move_mod(m, to, copy, mods);
    if (!r.ok) {
        show_error(u"⚠ %1 “%2”: %3"_s.arg(copy ? u"Copy"_s : u"Move"_s, m.name, r.error));
        drop_note->hide();
        if (r.folder.isEmpty() || !fs::isdir(r.folder))
            return false;
    } else {
        show_error(u""_s);
    }
    if (r.ok) {
        drop_note->setText(u"%1 “%2” → %3   (its .mod file: %4)"_s.arg(
            copy ? u"Copied"_s : u"Moved"_s, r.name, r.folder, pypath::basename(r.modfile)));
        drop_note->show();
    }
    if (modcopy::is_safe_dir(pypath::basename(to)))   // (the group it went into shows open)
        _open_safe.insert(pypath::normcase(pypath::abspath(pypath::dirname(to))));
    settings.setValue(u"modFolder"_s, r.folder);
    QTimer::singleShot(0, this, [this] { fill_mods(); });   // (not inside the drop event: the rows go away)
    return r.ok;
}

void OpenDialog::show_error(const QString &text)
{
    err->setText(text);
    err->setVisible(!text.isEmpty());
}

// a "Safeguards" row carries its mod's normcase folder in SAFE_HEAD; the safeguards' own rows carry it in SAFE_OF
static constexpr int SAFE_HEAD = Qt::UserRole + 1, SAFE_OF = Qt::UserRole + 2;

void OpenDialog::fill_mods()
{
    dir_label->setText(!mods_dir.isEmpty() ? mods_dir : u"(not set — click Change…)"_s);
    mod_list->clear();
    sc_list->clear();
    // (C++ only) every mod folder in the list, the base game from its own path or found next to one of them
    QStringList dirs;
    {
        QSet<QString> seen;
        for (const QString &d : QStringList{mods_dir} + more_dirs)
            if (!d.isEmpty() && fs::isdir(d) && !seen.contains(pypath::normcase(pypath::abspath(d)))) {
                seen.insert(pypath::normcase(pypath::abspath(d)));
                dirs << d;
            }
    }
    base.reset();
    if (!base_set.isEmpty() && moddata::is_game_folder(base_set))
        base = base_set;
    else
        for (const QString &d : dirs)
            if ((base = moddata::find_base_game(d)))
                break;
    game_label->setText(!base_set.isEmpty() && base ? base_set
                        : !base_set.isEmpty()       ? u"%1 — not a Victoria 2 folder"_s.arg(base_set)
                        : base                      ? u"%1   (found next to the mod folder)"_s.arg(*base)
                                                    : u"not found — click Change… and pick the Victoria 2 folder"_s);
    game_auto->setVisible(!base_set.isEmpty());
    mods.clear();
    group_of.clear();
    if (dirs.isEmpty())
        return;
    for (const QString &d : dirs) {
        auto [found, _b] = moddata::discover_mods(d, base);
        for (ModEntry &m : found) {
            mods.push_back(std::move(m));
            group_of.push_back(d);
        }
    }
    // C++ only: the safeguards (modcopy) — with a .mod file they are in `mods` already; one whose .mod file is gone is
    // added from its folder. Each goes under its mod, not in the list of mods.
    QHash<QString, std::vector<size_t>> safe;         // the mod's normcase folder -> its safeguards (indexes)
    {
        QSet<QString> listed;
        for (const ModEntry &m : mods)
            if (m.folder)
                listed.insert(pypath::normcase(pypath::abspath(*m.folder)));
        const size_t n0 = mods.size();
        for (size_t i = 0; i < n0; ++i)
            if (mods[i].folder && modcopy::safeguard_owner(*mods[i].folder).isEmpty())
                for (const QString &sg : modcopy::safeguards(*mods[i].folder))
                    if (!listed.contains(pypath::normcase(sg))) {
                        ModEntry d = moddata::_describe(pypath::basename(sg), sg, std::nullopt, base);
                        d.deps = mods[i].deps;
                        d.replace = mods[i].replace;
                        mods.push_back(std::move(d));
                        group_of.push_back(group_of[i]);
                    }
        for (size_t i = 0; i < mods.size(); ++i)
            if (mods[i].folder) {
                const QString owner = modcopy::safeguard_owner(*mods[i].folder);
                if (!owner.isEmpty())
                    safe[pypath::normcase(owner)].push_back(i);
            }
        for (auto it = safe.begin(); it != safe.end(); ++it)   // the newest first (the date in the folder's name)
            std::stable_sort(it->begin(), it->end(), [&](size_t a, size_t b) {
                return pypath::basename(*mods[a].folder).toLower() > pypath::basename(*mods[b].folder).toLower();
            });
    }
    const QString last = settings.value(u"modFolder"_s, u""_s).toString();
    const QBrush dim{QColor(theme::INK_FAINT)};
    QListWidgetItem *select = nullptr;
    std::vector<size_t> order;
    for (size_t i = 0; i < mods.size(); ++i)
        if (!mods[i].folder || modcopy::safeguard_owner(*mods[i].folder).isEmpty())
            order.push_back(i);
    std::vector<QString> low(mods.size());
    for (size_t i = 0; i < mods.size(); ++i)
        low[i] = py::lower(mods[i].name);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (mods[a].ok != mods[b].ok)
            return mods[a].ok;                    // (not ok) False first
        return low[a] < low[b];
    });
    auto add = [&](size_t i, const QString &indent, bool may_select) {
        const ModEntry &m = mods[i];
        const QString text = indent + m.name + (!m.note.isEmpty() ? u"    — "_s + m.note : u""_s);
        auto *it = new QListWidgetItem(text);
        it->setData(Qt::UserRole, int(i));
        it->setData(GROUP_DIR, group_of[i]);
        it->setToolTip(m.folder && !m.folder->isEmpty() ? *m.folder : m.modfile.value_or(u""_s));
        if (!m.ok) {
            it->setForeground(dim);
            if (m.deps.isEmpty())                   // (a submod that cannot be opened can still be deleted)
                it->setFlags(it->flags() & ~Qt::ItemIsSelectable);
        }
        mod_list->addItem(it);
        const bool was_last = !last.isEmpty() && pypath::normcase(m.folder.value_or(u""_s)) == pypath::normcase(last);
        if (m.ok && ((!select && may_select) || was_last))
            select = it;
        return it;
    };
    const QString game_mods = base ? pypath::normcase(pypath::join(*base, u"mod"_s)) : QString();
    for (const QString &d : dirs) {
        const QString dn = pypath::normcase(pypath::abspath(d));
        if (dirs.size() > 1) {                   // (C++ only) "all below are from <path>"
            const bool game = dn == game_mods;
            auto *sep = new QListWidgetItem();    // (its text: _sep_texts, the path shortened to the list's width)
            sep->setData(SEP_DIR, d);
            sep->setData(SEP_GAME, game);
            sep->setFlags(Qt::NoItemFlags);
            sep->setForeground(QBrush(QColor(theme::GOLD)));
            sep->setToolTip(d + (game ? u"\nThe game's own mod folder: the game plays these."_s
                                      : u"\nOutside the game's mod folder: the editor edits these here; to play one, drag "
                                        u"it into the game's mod folder (Ctrl: a copy)."_s) +
                            u"\nDrop a mod here: it moves into this folder (Ctrl: a copy)."_s +
                            (d == mods_dir ? u""_s : u"\nDouble-click: take this folder off the list (nothing is deleted)."_s));
            mod_list->addItem(sep);
        }
        bool any = false;
        for (size_t i : order) {
            if (pypath::normcase(pypath::abspath(group_of[i])) != dn)
                continue;
            any = true;
            add(i, QString(), true);
            const QString key = mods[i].folder ? pypath::normcase(pypath::abspath(*mods[i].folder)) : QString();
            const auto sg = safe.constFind(key);
            if (key.isEmpty() || sg == safe.constEnd())
                continue;
            // the "Safeguards" row: not a mod (no Qt::UserRole), not selectable; a click folds the group open / shut
            auto *head = new QListWidgetItem();
            head->setData(SAFE_HEAD, key);
            head->setFlags(Qt::NoItemFlags);
            head->setForeground(QBrush(QColor(theme::GOLD)));
            head->setToolTip(u"Untouched copies of \"%1\", one for each editing session, made before the editor's first "
                             u"change (\"Keep a copy of the original mod\", next to Apply). They lie inside the mod, in "
                             u"\"%2\"; the game never reads that folder. Each is a mod of its own in the game's launcher."
                             u"\nDrop a mod here: it moves into these safeguards (Ctrl+drag the mod itself here: a new "
                             u"safeguard of it)."_s
                                 .arg(mods[i].name, modcopy::SAFE_DIR));
            mod_list->addItem(head);
            for (size_t j : *sg) {
                QListWidgetItem *it = add(j, u"            "_s, false);
                it->setData(SAFE_OF, key);
                if (it == select)
                    _open_safe.insert(key);             // (the safeguard opened last time: its group shows)
            }
        }
        if (!any && dirs.size() > 1) {
            auto *none = new QListWidgetItem(u"      (no mods here yet — drag one in)"_s);
            none->setData(GROUP_DIR, d);
            none->setFlags(Qt::NoItemFlags);
            none->setForeground(dim);
            mod_list->addItem(none);
        }
    }
    _toggle_safe(nullptr);                          // (texts and hidden rows as `_open_safe` says)
    _sep_texts();
    if (select)
        mod_list->setCurrentItem(select);
}

void OpenDialog::_toggle_safe(QListWidgetItem *head)
{
    if (head) {
        const QString key = head->data(SAFE_HEAD).toString();
        if (!_open_safe.remove(key))
            _open_safe.insert(key);
    }
    QListWidgetItem *owner_row = nullptr;
    for (int r = 0; r < mod_list->count(); ++r) {
        QListWidgetItem *it = mod_list->item(r);
        if (const QVariant h = it->data(SAFE_HEAD); h.isValid()) {
            int n = 0;
            for (int k = r + 1; k < mod_list->count() && mod_list->item(k)->data(SAFE_OF) == h; ++k)
                ++n;
            const bool open = _open_safe.contains(h.toString());
            it->setText(u"      %1  Safeguards — %2 untouched cop%3"_s.arg(open ? u"▾"_s : u"▸"_s).arg(n).arg(
                n == 1 ? u"y"_s : u"ies"_s));
        } else if (const QVariant of = it->data(SAFE_OF); of.isValid()) {
            const bool open = _open_safe.contains(of.toString());
            if (!open && it == mod_list->currentItem())
                owner_row = it;
            it->setHidden(!open);
        }
    }
    if (owner_row) {                                // (shut over the chosen safeguard: its mod is chosen)
        const QVariant of = owner_row->data(SAFE_OF);
        for (int k = mod_list->row(owner_row) - 1; k > 0; --k)
            if (mod_list->item(k)->data(SAFE_HEAD) == of) {
                mod_list->setCurrentItem(mod_list->item(k - 1));
                break;
            }
    }
}

bool OpenDialog::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == mod_list->viewport() && (ev->type() == QEvent::DragEnter || ev->type() == QEvent::DragMove)) {
        auto *de = static_cast<QDropEvent *>(ev);   // (C++ only) where the dragged mod would go
        if (de->source() != mod_list) {
            ev->ignore();
            return true;
        }
        const ModEntry *m = _entry_of(mod_list->currentItem());
        const QString to = target_dir(mod_list->itemAt(de->position().toPoint()));
        const bool copy = de->modifiers() & Qt::ControlModifier;
        if (m && !to.isEmpty()) {
            drop_note->setText(u"Drop: %1 “%2” into %3%4"_s.arg(copy ? u"copy"_s : u"move"_s, m->name, to,
                                                                 copy ? QString() : u"   (Ctrl: a copy)"_s));
            drop_note->show();
            de->setDropAction(copy ? Qt::CopyAction : Qt::MoveAction);
            de->accept();
        } else {
            de->ignore();
        }
        return true;
    }
    if (obj == mod_list->viewport() && ev->type() == QEvent::Resize)
        _sep_texts();                               // (C++ only)
    if (obj == mod_list->viewport() && ev->type() == QEvent::DragLeave) {
        drop_note->hide();
        return true;
    }
    if (obj == mod_list->viewport() && ev->type() == QEvent::Drop) {
        auto *de = static_cast<QDropEvent *>(ev);
        const bool copy = de->modifiers() & Qt::ControlModifier;
        QListWidgetItem *src = de->source() == mod_list ? mod_list->currentItem() : nullptr;
        QListWidgetItem *t = mod_list->itemAt(de->position().toPoint());
        de->setDropAction(Qt::CopyAction);       // (never MoveAction: the list would take the dragged row out itself)
        de->accept();
        if (src)
            drop_on(src, t, copy);
        return true;
    }
    if (obj == mod_list->viewport() &&
        (ev->type() == QEvent::MouseButtonPress || ev->type() == QEvent::MouseButtonDblClick)) {
        auto *me = static_cast<QMouseEvent *>(ev);
        if (QListWidgetItem *it = mod_list->itemAt(me->position().toPoint()); it && it->data(SEP_DIR).isValid()) {
            if (ev->type() == QEvent::MouseButtonDblClick && me->button() == Qt::LeftButton &&
                it->data(SEP_DIR).toString() != mods_dir) {
                const QString d = it->data(SEP_DIR).toString();
                QTimer::singleShot(0, this, [this, d] { remove_dir(d); });
            }
            return true;                            // (a folder's line is no mod)
        }
        if (QListWidgetItem *it = mod_list->itemAt(me->position().toPoint()); it && it->data(SAFE_HEAD).isValid()) {
            if (me->button() == Qt::LeftButton)
                _toggle_safe(it);
            return true;                            // (the row is no mod: the chosen mod stays chosen)
        }
    }
    return QDialog::eventFilter(obj, ev);
}

void OpenDialog::mod_changed(QListWidgetItem *cur)
{
    sc_list->clear();
    mod.reset();
    entry.reset();
    const ModEntry *m = _entry_of(cur);
    sub_btn->setEnabled(m && m->ok);
    del_btn->setEnabled(m && !m->deps.isEmpty());
    del_btn->setText(u"Delete this submod…"_s);
    _del_armed.reset();
    sc_hint->setText(SC_HINT);
    QString safe_note;
    if (m && m->folder && !modcopy::safeguard_owner(*m->folder).isEmpty()) {   // C++ only: a safeguard
        const QString owner = modcopy::safeguard_owner(*m->folder);
        QString of = pypath::basename(owner);
        for (const ModEntry &x : mods)
            if (x.folder && pypath::normcase(*x.folder) == pypath::normcase(owner))
                of = x.name;
        safe_note = (u"SAFEGUARD of %1 — the mod as it was before the editor's first change in one editing "
                         u"session, kept inside it (%2). %3\nOpened here it shows that old state; a change made in it "
                         u"is saved into this copy (not into %1)."_s
                             .arg(of, modcopy::SAFE_DIR,
                                  m->modfile ? u"In the game's launcher: \"%1\"."_s.arg(m->name)
                                             : u"Its .mod file is gone: the launcher does not list it."_s));
        sc_hint->setText(safe_note);
    }
    if (m && base && (!base_set.isEmpty() || !more_dirs.isEmpty())) {   // (C++ only) edited here, played from the game
        const QString g = group_of[size_t(m - mods.data())];
        if (pypath::normcase(pypath::abspath(g)) != pypath::normcase(pypath::join(*base, u"mod"_s)) &&
            modcopy::safeguard_owner(m->folder.value_or(u""_s)).isEmpty())
            sc_hint->setText(sc_hint->text() + u"\nOUTSIDE the game's mod folder (%1): the editor edits it there; the "
                             u"game plays only the mods in %2 — drag it there to play it (Ctrl+drag: a copy)."_s.arg(
                                 g, pypath::join(*base, u"mod"_s)));
    }
    if (m && !m->deps.isEmpty()) {
        entry_sel = *m;
        const auto files = m->folder ? submods::holds(*m->folder) : decltype(submods::holds(u""_s)){};
        sc_hint->setText(
            u"SUBMOD of %1 — %2. %3.\nIn the editor you see the parent's files; only what you change is saved "
            u"into the submod. In the game's launcher tick the parent AND the submod. The start dates below are "
            u"the parent's."_s.arg(m->deps.join(u", "_s),
                                   m->folder ? u"its folder holds %1 file%2"_s.arg(files.size()).arg(
                                                   files.size() == 1 ? u""_s : u"s"_s)
                                             : u"its folder is missing (only the .mod file is left — it can be deleted)"_s,
                                   py::capitalize(submods::verdict(files).second)));
        if (!safe_note.isEmpty())                   // (a safeguard of a submod: both)
            sc_hint->setText(safe_note + u"\n"_s + sc_hint->text());
    }
    if (!m || !m->ok) {
        ok_btn->setEnabled(false);
        new_btn->setEnabled(false);
        return;
    }
    entry = *m;
    const ModEntry *look = m;
    std::vector<ModEntry> layers;
    if (!m->deps.isEmpty()) {                  // a submod: its start dates are those of the highest layer that has pops
        try {
            layers = submods::chain(mods, *m);
        } catch (const py::ValueError &e) {
            show_error(u"⚠ %1: %2"_s.arg(m->name, err_text(e)));
            ok_btn->setEnabled(false);
            new_btn->setEnabled(false);
            return;
        }
        look = &layers[0];
        for (auto x = layers.rbegin(); x != layers.rend(); ++x)
            if (fs::isdir(pypath::join(x->folder.value_or(u""_s), {u"history"_s, u"pops"_s}))) {
                look = &*x;
                break;
            }
    }
    mod = std::make_shared<Mod>(look->folder.value_or(u""_s), base, m->name, look->replace);
    try {
        mod->loc = mod->_load_localisation();
    } catch (const std::exception &) {
        mod->loc = {};
    }
    const QVariant sel = settings.value(u"scenario/"_s + pypath::normcase(m->folder.value_or(u""_s)));
    fill_scenarios(sel.isValid() ? OptStr(sel.toString()) : std::nullopt);
    if (!m->deps.isEmpty())
        new_btn->setEnabled(false);                 // (a new start date is made in the parent mod)
}

void OpenDialog::fill_scenarios(const OptStr &select)
{
    sc_list->clear();
    const auto scen = mod->scenarios();
    for (const auto &s : scen) {
        QString text = u"%1    %2 pop files"_s.arg(s.name).arg(s.files);
        if (s.bookmark && !s.bookmark->isEmpty())
            text += u"    — %1"_s.arg(*s.bookmark);
        if (!s.isDate)
            text += u"    (not a date: the game won't load it)"_s;
        auto *it = new QListWidgetItem(text);
        it->setData(Qt::UserRole, s.name);
        sc_list->addItem(it);
        if (select && s.name == *select)
            sc_list->setCurrentItem(it);
    }
    if (!sc_list->currentItem() && sc_list->count()) {
        // prefer the first real date folder that has files
        int best = 0;
        for (size_t i = 0; i < scen.size(); ++i)
            if (scen[i].isDate && scen[i].files) {
                best = int(i);
                break;
            }
        sc_list->setCurrentRow(best);
    }
    ok_btn->setEnabled(!scen.empty());
    new_btn->setEnabled(true);
}

void OpenDialog::new_scenario()
{
    if (!mod)
        return;
    const OptStr src = scenario();
    bool ok = false;
    const QString name = QInputDialog::getText(this, u"New scenario"_s,
                                               u"Folder name for the new start date (e.g. 1861.1.1).\n"
                                               u"Pop files are copied from '%1'."_s.arg(src && !src->isEmpty() ? *src : u"nothing"_s),
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok || py::strip(name).isEmpty())
        return;
    try {
        mod->create_scenario(py::strip(name), src);
    } catch (const std::exception &e) {
        show_error(u"⚠ New scenario: %1"_s.arg(err_text(e)));
        return;
    }
    show_error(u""_s);
    fill_scenarios(py::strip(name));
}

void OpenDialog::new_submod()
{
    if (!entry)
        return;
    const ModEntry m = *entry;
    bool ok = false;
    const QString name = QInputDialog::getText(this, u"New submod"_s,
                                               u"Name of the new submod of '%1'.\n"
                                               u"It starts empty: what you change in it is all it will hold."_s.arg(m.name),
                                               QLineEdit::Normal, u"%1 submod"_s.arg(m.name), &ok);
    if (!ok || py::strip(name).isEmpty())
        return;
    QString folder;
    try {
        const ModEntry *cm = _entry_of(mod_list->currentItem());   // (C++ only: next to its parent, in its folder)
        folder = submods::create(cm ? group_of[size_t(cm - mods.data())] : mods_dir, m, name).first;
    } catch (const py::ValueError &e) {
        show_error(u"⚠ New submod: %1"_s.arg(err_text(e)));
        return;
    } catch (const fs::OSError &e) {
        show_error(u"⚠ New submod: %1"_s.arg(err_text(e)));
        return;
    }
    show_error(u""_s);
    settings.setValue(u"modFolder"_s, folder);
    fill_mods();
}

void OpenDialog::new_base_mod()
{
    if (mods_dir.isEmpty())
        return;
    if (!base) {
        show_error(u"⚠ New mod: the base game was not found next to this mod folder (it has to be the game's own "
                   u"'mod' folder — or pick the game under “Base game”)."_s);
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, u"New mod"_s,
                                               u"Name of the new mod.\n"
                                               u"It starts from the base game: what you change in it is all it will hold."_s,
                                               QLineEdit::Normal, u"My mod"_s, &ok);
    if (!ok || py::strip(name).isEmpty())
        return;
    QString folder;
    try {
        folder = submods::create_new(mods_dir, name).first;
    } catch (const py::ValueError &e) {
        show_error(u"⚠ New mod: %1"_s.arg(err_text(e)));
        return;
    } catch (const fs::OSError &e) {
        show_error(u"⚠ New mod: %1"_s.arg(err_text(e)));
        return;
    }
    modcopy::mark_copied(folder);                // (nothing to keep a copy of: it was just made)
    show_error(u""_s);
    settings.setValue(u"modFolder"_s, folder);
    fill_mods();
}

void OpenDialog::delete_submod()
{
    const ModEntry *mp = _entry_of(mod_list->currentItem());
    if (!mp || mp->deps.isEmpty())
        return;
    const ModEntry m = *mp;
    if (!_del_armed || monotonic() - *_del_armed > 10) {
        _del_armed = monotonic();
        del_btn->setText(u"Click again: “%1” goes to the Recycle Bin"_s.arg(m.name));
        return;
    }
    _del_armed.reset();
    const QString work = pypath::join(app_dir(), {submods::WORK_FOLDER,
                                                  pypath::basename(m.folder && !m.folder->isEmpty()
                                                                       ? *m.folder : py::slice(m.modfile.value_or(u""_s), 0, -4))});
    QStringList failed;
    for (const QString &path : {m.folder.value_or(u""_s), m.modfile.value_or(u""_s), work, submods::state_file(work)})
        if (!path.isEmpty() && fs::exists(path) && !QFile::moveToTrash(path))
            failed << path;
    show_error(!failed.isEmpty() ? u"⚠ Could not be moved to the Recycle Bin: %1"_s.arg(failed.join(u", "_s)) : u""_s);
    fill_mods();
}

OptStr OpenDialog::scenario() const
{
    QListWidgetItem *it = sc_list->currentItem();
    return it ? OptStr(it->data(Qt::UserRole).toString()) : std::nullopt;
}

void OpenDialog::try_accept()
{
    if (mod && scenario() && !scenario()->isEmpty()) {
        const QString folder = entry ? entry->folder.value_or(u""_s) : mod->root;
        settings.setValue(u"modsDir"_s, mods_dir);
        settings.setValue(u"modFolder"_s, folder);
        settings.setValue(u"scenario/"_s + pypath::normcase(folder), *scenario());
        accept();
    }
}

// ---------------------------------------------------------------------------------------------- loading
std::pair<std::shared_ptr<Mod>, OptStr> open_submod(const std::vector<ModEntry> &mods, const ModEntry &entry,
                                                    const OptStr &base, const OptStr &work_root,
                                                    const OptStr &backup_root_)
{
    Splash splash;
    splash.show();
    std::shared_ptr<Mod> mod;
    try {
        const auto layers = submods::chain(mods, entry);
        QStringList roots;
        for (const ModEntry &m : layers)
            roots << m.folder.value_or(u""_s);
        const QString folder = entry.folder.value_or(u""_s);
        const QString work = pypath::join(work_root && !work_root->isEmpty() ? *work_root
                                                                             : pypath::join(app_dir(), submods::WORK_FOLDER),
                                          pypath::basename(folder));
        const QString backup_root = backup_root_ && !backup_root_->isEmpty()
                                        ? *backup_root_
                                        : pypath::join(app_dir(), {u"backups"_s, safe_name(pypath::basename(folder))});
        splash.say(u"Preparing the submod's working copy…"_s);
        const auto saved = submods::recover(work, roots, backup_root);
        const auto info = submods::prepare(roots, work, [&splash](const QString &t) { splash.say(t); });
        QStringList replace;
        for (const ModEntry &m : layers)
            for (const QString &r : m.replace)
                if (!replace.contains(r))
                    replace << r;
        mod = std::make_shared<Mod>(work, base, entry.name, replace);
        auto sub = std::make_shared<submods::Submod>();
        sub->root = pypath::abspath(folder);
        sub->name = entry.name;
        for (size_t i = 0; i + 1 < layers.size(); ++i) {
            sub->parents << layers[i].name;
            sub->parent_roots << roots[qsizetype(i)];
        }
        sub->view = info.view;
        sub->state = info.state;
        sub->work = work;
        sub->recovered = saved;
        mod->submod = sub;
    } catch (const py::ValueError &e) {
        splash.close();
        return {nullptr, u"⚠ Could not open the submod %1: %2"_s.arg(entry.name, err_text(e))};
    } catch (const fs::OSError &e) {
        splash.close();
        return {nullptr, u"⚠ Could not open the submod %1: %2"_s.arg(entry.name, err_text(e))};
    }
    splash.close();
    return {mod, std::nullopt};
}

std::pair<std::shared_ptr<mapview::MapData>, OptStr> load_mod(Mod &mod)
{
    Splash splash;
    splash.show();
    std::shared_ptr<mapview::MapData> mapdata;
    try {
        splash.say(u"Reading %1…"_s.arg(mod.name()));
        mod.warnings.clear();
        mod.load();
        splash.say(u"Reading province map…"_s);
        mapdata = std::make_shared<mapview::MapData>(mod, mod.p({u"map"_s, mod.provinces_bmp}));
    } catch (const std::exception &e) {
        splash.close();
        // (Python adds the last four lines of its traceback; C++ has none — the message stands alone)
        return {nullptr, u"⚠ Could not load %1: %2"_s.arg(mod.name(), err_text(e))};
    }
    splash.close();
    return {mapdata, std::nullopt};
}

// ---------------------------------------------------------------------------------------------- the error log
void log_error(const QString &text)
{
    const QString path = pypath::join(app_dir(), u"errors.log"_s);
    QFile f(path);
    if (f.open(QIODevice::Append)) {
        f.write(u"\n=== %1 ===\n%2\n"_s.arg(QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm:ss"_s), text)
                    .toUtf8());
    }
    std::fprintf(stderr, "%s\n", text.toUtf8().constData());
}

App::App(int &argc, char **argv) : QApplication(argc, argv) {}

bool App::notify(QObject *receiver, QEvent *ev)
{
    try {
        return QApplication::notify(receiver, ev);
    } catch (const std::exception &e) {
        log_error(u"Exception (%1, event %2): %3"_s.arg(receiver ? receiver->metaObject()->className() : "?")
                      .arg(int(ev->type()))
                      .arg(err_text(e)));
    } catch (...) {
        log_error(u"Exception of an unknown kind (%1, event %2)"_s.arg(
            receiver ? receiver->metaObject()->className() : "?").arg(int(ev->type())));
    }
    return false;
}

int main(int argc, char *argv[])
{
    App qapp(argc, argv);
    qapp.setApplicationName(u"ModEditor"_s);
    qapp.setApplicationVersion(QStringLiteral(MODEDITOR_VERSION_TEXT));
    theme::apply(qapp);
    qapp.setWindowIcon(make_icon());
    auto *no_wheel = new NoWheelFilter(&qapp);
    qapp.installEventFilter(no_wheel);
    auto settings = settings_::settings();

    QStringList args;
    for (const QString &a : qapp.arguments().mid(1))
        if (!a.startsWith(u'-'))
            args << a;
    QString mods_dir = !args.isEmpty() && has_mods(args[0]) ? args[0] : guess_mods_dir(*settings);

    if (qapp.arguments().contains(u"--smoke"_s)) {   // (tests: start, show the start window once, quit)
        OpenDialog dlg(*settings, mods_dir);
        dlg.show();
        QApplication::processEvents();
        return 0;
    }
    update::Checker checker(*settings);          // "a new version is out": asks GitHub at most once a day, if ticked
    checker.start();
    OptStr error;
    while (true) {
        OpenDialog dlg(*settings, mods_dir, error);
        if (dlg.exec() != QDialog::Accepted)
            return 0;
        mods_dir = dlg.mods_dir;
        std::shared_ptr<Mod> mod = dlg.mod;
        const OptStr scenario = dlg.scenario();
        if (dlg.entry && !dlg.entry->deps.isEmpty()) {
            std::tie(mod, error) = open_submod(dlg.mods, *dlg.entry, dlg.base);
            if (!mod)
                continue;
        }
        auto [mapdata, err] = load_mod(*mod);
        error = err;
        if (!mapdata)
            continue;
        const QString backups = pypath::join(app_dir(), {u"backups"_s, safe_name(pypath::basename(mod->root))});
        auto win = std::make_unique<mainwindow::MainWindow>(mod, mapdata, scenario, backups);
        win->showMaximized();
        if (const auto &sub = mod->submod) {
            const auto saved = sub->recovered;
            sub->recovered.reset();
            win->message(u"Submod of %1 — Apply writes only what differs from the parent into %2."_s.arg(
                             sub->parents.join(u", "_s), sub->root) +
                             (saved ? u" Carried over from the last session: %1 file(s) written, %2 taken out."_s
                                          .arg(saved->first.size()).arg(saved->second.size())
                                    : u""_s),
                         25000);
        }
        qapp.exec();
        while (win->reload_requested) {           // the map was cut / provinces renumbered: read again
            const QString note = win->reload_note;
            auto again = std::make_shared<Mod>(mod->root, mod->base, mod->name(), mod->replace);
            again->submod = mod->submod;              // (a submod stays one over a re-read)
            auto [md, e2] = load_mod(*again);
            error = e2;
            if (!md)
                break;
            mod = again;
            win = std::make_unique<mainwindow::MainWindow>(mod, md, scenario, backups);
            win->showMaximized();
            if (!note.isEmpty())                       // (what the last window did, said in the new one)
                win->message(note, 30000);
            qapp.exec();
        }
        if (!win->switch_requested && !win->reload_requested)
            return 0;
    }
}

}  // namespace app
