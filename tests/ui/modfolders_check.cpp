// The opening window's folders (C++ only, 2026-11-17): a base game of its own, more mod folders under lines "All below
// are from …", mods moved / copied between the folders and the safeguards (modfolders.h), with their .mod files.
// Usage: med_modfolders_check <scratch folder (made anew)> <shots>   (MODEDITOR_SETTINGS of its own; the base game is
// the "Victoria 2" folder next to the port, read only)
#include "../core/nopopup.h"
#include "app/app.h"
#include "core/modcopy.h"
#include "core/modfolders.h"
#include "core/pypath.h"
#include "core/settings.h"
#include "ui/theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <cstdio>

using namespace Qt::StringLiterals;

static int fails = 0;
static void ok(bool c, const QString &what)
{
    std::printf("%s %s\n", c ? "ok  " : "FAIL", qUtf8Printable(what));
    std::fflush(stdout);
    if (!c)
        ++fails;
}

static void put(const QString &path, const QByteArray &data)
{
    QDir().mkpath(pypath::dirname(path));
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(data);
}

static QString text(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll()) : QString();
}

static void a_mod(const QString &folder)
{
    put(pypath::join(folder, u"history/pops/1836.1.1/pops.txt"_s), "1 = { }\r\n");
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    if (argc < 3)
        return 2;
    const QString root = QString::fromLocal8Bit(argv[1]), shots = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(shots);
    const QString port = pypath::dirname(pypath::dirname(pypath::normpath(QCoreApplication::applicationDirPath())));
    const QString game = pypath::join(pypath::dirname(port), u"Victoria 2"_s);
    const QString main_d = pypath::join(root, u"main"_s), work = pypath::join(root, u"work"_s),
                  empty = pypath::join(root, u"empty"_s);
    QDir().mkpath(empty);
    a_mod(pypath::join(main_d, u"Alpha"_s));
    put(pypath::join(main_d, u"Alpha.mod"_s), "name = \"Alpha\"\r\npath = \"mod/Alpha\"\r\nuser_dir = \"Alpha\"\r\n");
    const QString sg_name = u"Alpha (original 2026-11-17 1200)"_s;
    a_mod(pypath::join(main_d, {u"Alpha"_s, modcopy::SAFE_DIR, sg_name}));
    put(pypath::join(main_d, sg_name + u".mod"_s),
        ("name = \"Alpha (original 2026-11-17 12:00)\"\r\npath = \"mod/Alpha/ModEditor safeguards/" + sg_name.toLatin1() +
         "\"\r\n"));
    a_mod(pypath::join(work, u"Beta"_s));         // (a mod folder without a .mod file)

    auto st = settings_::settings();
    st->setValue(u"baseGame"_s, game);
    st->remove(u"moreModDirs"_s);
    app::OpenDialog dlg(*st, main_d);
    dlg.resize(1100, 640);
    dlg.show();
    QApplication::processEvents();
    auto find = [&](const QString &starts) -> QListWidgetItem * {
        for (int i = 0; i < dlg.mod_list->count(); ++i) {
            QListWidgetItem *it = dlg.mod_list->item(i);
            if (it->text().trimmed().startsWith(starts))
                return it;
        }
        return nullptr;
    };
    auto sep_of = [&](const QString &path) -> QListWidgetItem * {   // a folder's line (its text is shortened)
        for (int i = 0; i < dlg.mod_list->count(); ++i)
            if (dlg.mod_list->item(i)->data(Qt::UserRole + 3).toString() == path)
                return dlg.mod_list->item(i);
        return nullptr;
    };
    auto settle = [] {
        for (int i = 0; i < 3; ++i)
            QApplication::processEvents();
    };
    auto drop = [&](QListWidgetItem *src, QListWidgetItem *t, bool copy) {
        std::printf("     drop %s -> %s%s\n", src ? qUtf8Printable(src->text().trimmed()) : "(none)",
                    t ? qUtf8Printable(dlg.target_dir(t)) : "(none)", copy ? " (copy)" : "");
        dlg.drop_on(src, t, copy);
        if (!dlg.err->text().isEmpty())
            std::printf("     err: %s\n", qUtf8Printable(dlg.err->text()));
    };
    ok(dlg.game_label->text() == game && dlg.base == game, u"Base game: "_s + dlg.game_label->text());
    ok(!find(u"━━"_s), u"one folder: no 'All below are from' line"_s);

    // --- more folders
    dlg.add_dir(work);
    dlg.add_dir(empty);
    settle();
    QStringList seps;
    for (int i = 0; i < dlg.mod_list->count(); ++i)
        if (dlg.mod_list->item(i)->text().startsWith(u"━━"_s))
            seps << dlg.mod_list->item(i)->text();
    ok(seps.size() == 3, u"three lines: "_s + seps.join(u" | "_s));
    ok(st->value(u"moreModDirs"_s).toStringList().size() == 2, u"the two folders are kept in the settings"_s);
    QListWidgetItem *beta = find(u"Beta"_s);
    ok(beta && (beta->flags() & Qt::ItemIsSelectable), u"Beta (outside the game, no .mod file) can be opened: "_s +
                                                         (beta ? beta->text() : QString()));
    ok(find(u"(no mods here yet"_s) != nullptr, u"the empty folder says: no mods here yet"_s);
    dlg.mod_list->setCurrentItem(beta);
    settle();
    ok(dlg.sc_hint->text().contains(u"OUTSIDE the game's mod folder"_s), u"Beta: 'outside the game's mod folder'"_s);
    dlg.grab().save(pypath::join(shots, u"1 folders.png"_s));

    // --- move Beta into the first folder (onto its line)
    drop(beta, sep_of(main_d), false);
    settle();
    ok(QDir(pypath::join(main_d, u"Beta"_s)).exists() && !QDir(pypath::join(work, u"Beta"_s)).exists(),
       u"Beta moved: work → main"_s);
    ok(text(pypath::join(main_d, u"Beta.mod"_s)).contains(u"path = \"mod/Beta\""_s),
       u"…with a new .mod file: "_s + text(pypath::join(main_d, u"Beta.mod"_s)).simplified());
    ok(dlg.drop_note->text().startsWith(u"Moved"_s), u"note: "_s + dlg.drop_note->text());

    // --- move Beta onto Alpha's safeguards
    QListWidgetItem *head = nullptr;
    for (int i = 0; i < dlg.mod_list->count(); ++i)
        if (dlg.mod_list->item(i)->text().contains(u"Safeguards —"_s))
            head = dlg.mod_list->item(i);
    ok(head != nullptr, u"Alpha's safeguards row"_s);
    drop(find(u"Beta"_s), head, false);
    settle();
    const QString beta_sg = pypath::join(main_d, {u"Alpha"_s, modcopy::SAFE_DIR, u"Beta"_s});
    ok(QDir(beta_sg).exists(), u"Beta is a safeguard of Alpha now"_s);
    ok(text(pypath::join(main_d, u"Beta.mod"_s)).contains(u"path = \"mod/Alpha/ModEditor safeguards/Beta\""_s),
       u"…its .mod: "_s + text(pypath::join(main_d, u"Beta.mod"_s)).simplified());

    // --- the old safeguard out into the work folder: a mod of its own again
    QListWidgetItem *sg = find(u"Alpha (original 2026-11-17 12:00)"_s);
    ok(sg != nullptr, u"the safeguard is listed"_s);
    drop(sg, sep_of(work), false);
    settle();
    ok(QDir(pypath::join(work, sg_name)).exists() && QFile::exists(pypath::join(work, sg_name + u".mod"_s)) &&
           !QFile::exists(pypath::join(main_d, sg_name + u".mod"_s)),
       u"the safeguard → work, with its .mod file"_s);
    ok(text(pypath::join(work, sg_name + u".mod"_s)).contains(u"path = \"mod/"_s + sg_name + u'"'),
       u"…path: "_s + text(pypath::join(work, sg_name + u".mod"_s)).simplified());

    // --- Ctrl+drag Alpha onto its own safeguards: a new safeguard
    QListWidgetItem *in_sg = find(u"Beta"_s);
    const qsizetype before = modcopy::safeguards(pypath::join(main_d, u"Alpha"_s)).size();
    drop(find(u"Alpha"_s), in_sg, true);
    settle();
    ok(modcopy::safeguards(pypath::join(main_d, u"Alpha"_s)).size() == before + 1,
       u"Ctrl+drag Alpha onto its safeguards: a new one (%1 → %2)"_s.arg(before).arg(
           modcopy::safeguards(pypath::join(main_d, u"Alpha"_s)).size()));
    // --- plain drag into its own safeguards: refused
    drop(find(u"Alpha"_s), find(u"Beta"_s), false);
    settle();
    ok(dlg.err->text().contains(u"can't go into its own safeguards"_s), u"refused: "_s + dlg.err->text());
    // --- Ctrl+drag Alpha into the empty folder: a copy, without the safeguards
    drop(find(u"Alpha"_s), find(u"(no mods here yet"_s), true);
    settle();
    ok(QDir(pypath::join(empty, u"Alpha"_s)).exists() && QDir(pypath::join(main_d, u"Alpha"_s)).exists() &&
           !QDir(pypath::join(empty, {u"Alpha"_s, modcopy::SAFE_DIR})).exists(),
       u"Ctrl+drag: Alpha copied into 'empty', without its safeguards"_s);
    ok(text(pypath::join(empty, u"Alpha.mod"_s)).contains(u"user_dir = \"Alpha\""_s),
       u"…the .mod keeps its other lines: "_s + text(pypath::join(empty, u"Alpha.mod"_s)).simplified());
    // --- a copy where the name is taken: Alpha (2)
    drop(find(u"Alpha"_s), sep_of(empty), true);
    settle();
    ok(QDir(pypath::join(empty, u"Alpha (2)"_s)).exists() &&
           text(pypath::join(empty, u"Alpha (2).mod"_s)).contains(u"name = \"Alpha (2)\""_s),
       u"a taken name: Alpha (2)"_s);
    // --- move Alpha (with its safeguards) into work: their .mod files follow
    QListWidgetItem *alpha_main = nullptr;
    for (int i = 0; i < dlg.mod_list->count(); ++i)
        if (pypath::normcase(dlg.mod_list->item(i)->toolTip()) ==
                                                              pypath::normcase(pypath::join(main_d, u"Alpha"_s)))
            alpha_main = dlg.mod_list->item(i);
    drop(alpha_main, sep_of(work), false);
    settle();
    ok(QDir(pypath::join(work, {u"Alpha"_s, modcopy::SAFE_DIR, u"Beta"_s})).exists() &&
           text(pypath::join(work, u"Beta.mod"_s)).contains(u"path = \"mod/Alpha/ModEditor safeguards/Beta\""_s) &&
           !QFile::exists(pypath::join(main_d, u"Beta.mod"_s)),
       u"Alpha → work: its safeguard Beta's .mod file followed"_s);
    dlg.grab().save(pypath::join(shots, u"2 after the drags.png"_s));
    // --- the same folder: nothing happens
    QListWidgetItem *w_alpha = nullptr;
    for (int i = 0; i < dlg.mod_list->count(); ++i)
        if (pypath::normcase(dlg.mod_list->item(i)->toolTip()) == pypath::normcase(pypath::join(work, u"Alpha"_s)))
            w_alpha = dlg.mod_list->item(i);
    if (!w_alpha)
        for (int i = 0; i < dlg.mod_list->count(); ++i)
            std::printf("     row %s | %s\n", qUtf8Printable(dlg.mod_list->item(i)->text()),
                        qUtf8Printable(dlg.mod_list->item(i)->toolTip().left(160)));
    drop(w_alpha, sep_of(work), false);
    settle();
    ok(dlg.err->text().contains(u"in that folder already"_s), u"same folder: "_s + dlg.err->text());
    // --- a folder off the list: the files stay
    dlg.remove_dir(empty);
    settle();
    ok(!sep_of(empty) && QDir(pypath::join(empty, u"Alpha"_s)).exists(),
       u"'empty' taken off the list; its files are still there"_s);
    // --- with_path keeps the rest
    ok(modfolders::with_path(u"name = \"X\"\r\npath = \"mod/X\"\r\nreplace_path = \"a\"\r\n"_s, u"Y"_s) ==
           u"name = \"X\"\r\npath = \"mod/Y\"\r\nreplace_path = \"a\"\r\n"_s,
       u"with_path changes the path line only"_s);
    std::printf("%s\n", fails ? "FAILED" : "PASS");
    std::fflush(stdout);
    std::_Exit(fails ? 1 : 0);
}
