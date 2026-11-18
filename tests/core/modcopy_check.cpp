// modcopy: "keep a copy of the original mod" (C++ only, the user 2026-11-13). A small scratch mod in build/modcopy:
// the guard armed, a write into the mod → the untouched folder copied next to it (every file, the bytes from BEFORE the
// write), its own .mod (name "… (original …)", path, dependencies, user_dir "_original"); a second write makes no
// second copy; a write next to the mod, or with the tick off, or with MODEDITOR_KEEP_COPY=0, makes none; a copy that
// can't be made stops the write. Nothing outside build/modcopy is touched.
// Usage: med_modcopy_check <port folder>
#include "nopopup.h"
#include "app/app.h"
#include "core/applyjournal.h"
#include "core/fs.h"
#include "core/modcopy.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/pynum.h"
#include "core/submods.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <cstdio>

using namespace Qt::StringLiterals;

static int fails = 0;
static void check(bool ok, const QString &what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", qUtf8Printable(what));
    fails += int(!ok);
}

static void put(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(pypath::dirname(path));
    QFile f(fs::watched(path));                  // (as the editor's writers do)
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(bytes);
}

static bool same(const QString &a, const QString &b)
{
    return pypath::normcase(pypath::abspath(a)) == pypath::normcase(pypath::abspath(b));
}

static QByteArray get(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("(missing)");
}

// what holds "(original": names in the game's mod folder (the .mod files) and in the mod's safeguards folder
static QStringList originals(const QString &mods)
{
    QStringList out;
    for (const QString &n : QDir(mods).entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot))
        if (n.contains(u"(original"_s))
            out << n;
    for (const QString &f : modcopy::safeguards(pypath::join(mods, u"Test Mod"_s)))
        out << modcopy::SAFE_DIR + u"/"_s + pypath::basename(f);
    return out;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    if (argc < 2) {
        std::printf("usage: med_modcopy_check <port folder>\n");
        return 2;
    }
    const QString base = pypath::join(QString::fromLocal8Bit(argv[1]), u"build/modcopy"_s);
    QDir(base).removeRecursively();              // (our own scratch folder, made fresh)
    const QString mods = pypath::join(base, u"mod"_s), mod = pypath::join(mods, u"Test Mod"_s);
    QDir().mkpath(mod);
    // the mod: a few files in folders, and its .mod file
    {
        QFile f(pypath::join(mods, u"Test Mod.mod"_s));
        f.open(QIODevice::WriteOnly);
        f.write("name = \"Test Mod\"\npath = \"mod/Test Mod\"\nuser_dir = \"testmod\"\n"
                "dependencies = { \"Base Thing\" }\nreplace_path = \"history/pops\"\n");
    }
    const QByteArray old_pops = "province = { farmers = { culture = a religion = b size = 100 } }\n";
    for (const auto &[rel, bytes] :
         std::vector<std::pair<QString, QByteArray>>{{u"history/pops/1836.1.1/x.txt"_s, old_pops},
                                                     {u"map/definition.csv"_s, "province;red;green;blue;x;x\n"},
                                                     {u"common/goods.txt"_s, "goods = {}\n"}}) {
        QDir().mkpath(pypath::dirname(pypath::join(mod, rel)));
        QFile f(pypath::join(mod, rel));
        f.open(QIODevice::WriteOnly);
        f.write(bytes);
    }
    check(!modcopy::inside(pypath::join(mods, u"Test Mod (original 2026-11-13 2140)/a.txt"_s), mod),
          u"\"Test Mod (original …)\" is not inside \"Test Mod\""_s);
    check(modcopy::inside(pypath::join(mod, u"MAP\\Definition.csv"_s), mod), u"case and slashes as Windows treats them"_s);

    bool tick = true;
    int done_calls = 0;
    modcopy::Result last;
    const int id = modcopy::arm(mod, QString(), [&] { return tick; }, [&](const modcopy::Result &r) {
        ++done_calls;
        last = r;
    });
    // a write next to the mod: no copy
    put(pypath::join(mods, u"Other Mod/x.txt"_s), "x");
    check(originals(mods).isEmpty() && done_calls == 0, u"a write next to the mod makes no copy"_s);
    // the tick off: no copy
    tick = false;
    put(pypath::join(mod, u"common/goods.txt"_s), "goods = { a }\n");
    check(originals(mods).isEmpty(), u"the tick off: no copy"_s);
    put(pypath::join(mod, u"common/goods.txt"_s), "goods = {}\n");   // (back as it was)
    tick = true;
    // the environment switch: no copy
    qputenv("MODEDITOR_KEEP_COPY", "0");
    put(pypath::join(mod, u"common/goods.txt"_s), "goods = {}\n");
    check(originals(mods).isEmpty(), u"MODEDITOR_KEEP_COPY=0: no copy"_s);
    qunsetenv("MODEDITOR_KEEP_COPY");
    // the first write: the copy first, of the untouched mod
    const QByteArray new_pops = "province = { farmers = { culture = a religion = b size = 999 } }\n";
    put(pypath::join(mod, u"history/pops/1836.1.1/x.txt"_s), new_pops);
    const QStringList made = originals(mods);
    check(made.size() == 2 && done_calls == 1 && last.ok, u"the first write: one copy (folder + .mod): %1"_s.arg(made.join(u", "_s)));
    check(same(pypath::dirname(last.folder), pypath::join(mod, modcopy::SAFE_DIR)) &&
              same(pypath::dirname(last.modfile), mods),
          u"the copy INSIDE the mod (%1), its .mod in the game's mod folder"_s.arg(QDir(mods).relativeFilePath(last.folder)));
    check(same(modcopy::safeguard_owner(last.folder), mod) && modcopy::safeguard_owner(mod).isEmpty(),
          u"safeguard_owner: the copy belongs to the mod; the mod is no safeguard"_s);
    check(last.files == 3, u"all 3 files copied (%1)"_s.arg(last.files));
    check(get(pypath::join(last.folder, u"history/pops/1836.1.1/x.txt"_s)) == old_pops,
          u"the copy holds the file as it was BEFORE the write"_s);
    check(get(pypath::join(mod, u"history/pops/1836.1.1/x.txt"_s)) == new_pops, u"the mod holds the new text"_s);
    const QByteArray mf = get(last.modfile);
    check(mf.contains("name = \"Test Mod (original ") &&last.name.startsWith(u"Test Mod (original "_s),
          u"its .mod: the launcher name \"%1\""_s.arg(last.name));
    check(mf.contains(("path = \"mod/Test Mod/" + modcopy::SAFE_DIR + u"/"_s + pypath::basename(last.folder) + "\"").toUtf8()),
          u"its .mod: path to the copy inside the mod"_s);
    {   // the editor's own list of mods finds the copy through that .mod file
        const auto [found, _b] = moddata::discover_mods(mods);
        bool seen = false;
        for (const moddata::ModEntry &m : found)
            if (m.folder && pypath::normcase(*m.folder) == pypath::normcase(last.folder) && m.name == last.name)
                seen = !m.note.contains(u"not found"_s) && m.deps == QStringList{u"Base Thing"_s};
        check(seen, u"discover_mods: the copy is found through its .mod (path below the mod folder)"_s);
    }
    check(mf.contains("testmod_original"), u"its .mod: a user_dir of its own"_s);
    check(mf.contains("Base Thing") && mf.contains("history/pops"), u"its .mod: dependencies and replace_path kept"_s);
    check(get(pypath::join(mods, u"Test Mod.mod"_s)).contains("name = \"Test Mod\""), u"the mod keeps its name"_s);
    // a second write: no second copy
    put(pypath::join(mod, u"common/goods.txt"_s), "goods = { b }\n");
    check(originals(mods).size() == 2 && done_calls == 1, u"a second write: no second copy"_s);
    modcopy::disarm(id);
    // the safeguards folder is not the mod: the next session's copy, a submod's working copy, the Apply journal skip it
    {
        const modcopy::Result r2 = modcopy::keep_original(mod, QDateTime::currentDateTime().addSecs(3600));
        check(r2.ok && r2.files == 3 && !fs::isdir(pypath::join(r2.folder, modcopy::SAFE_DIR)),
              u"the next copy holds the mod's 3 files, not the copies before it (%1)"_s.arg(r2.files));
        check(modcopy::safeguards(mod).size() == 2 &&
                  pypath::basename(modcopy::safeguards(mod)[0]) == pypath::basename(r2.folder),
              u"safeguards(): both, the newest first"_s);
        bool clean = true;
        for (const auto &[k, _e] : submods::tree_state(mod))
            clean = clean && !k.startsWith(modcopy::SAFE_DIR.toLower());
        check(clean && submods::tree_state(mod).size() == 3, u"tree_state (submods' working copies) skips it"_s);
        bool clean2 = true;
        for (const auto &[k, _e] : applyjournal::_scan(mod).files)
            clean2 = clean2 && !k.toLower().startsWith(modcopy::SAFE_DIR.toLower());
        check(clean2, u"the Apply journal's scan skips it"_s);
        // a safeguard's own copy goes to the same safeguards folder (never one inside the copy)
        const modcopy::Result r3 = modcopy::keep_original(r2.folder, QDateTime::currentDateTime().addSecs(7200));
        check(r3.ok && same(pypath::dirname(r3.folder), pypath::join(mod, modcopy::SAFE_DIR)),
              u"a copy of a safeguard lies beside it: %1"_s.arg(pypath::basename(r3.folder)));
        QFile::remove(r3.modfile);                  // (its .mod file gone: the list adds it from its folder)
    }
    // the opening window: the mod once, its safeguards under it in a group that folds open and shut
    {
        QSettings st(pypath::join(base, u"settings.ini"_s), QSettings::IniFormat);
        app::OpenDialog dlg(st, mods);
        dlg.resize(940, 520);
        dlg.show();
        QApplication::processEvents();
        QListWidget *l = dlg.mod_list;
        int head = -1, tops = 0, subs = 0, hidden = 0;
        for (int r = 0; r < l->count(); ++r) {
            QListWidgetItem *it = l->item(r);
            if (it->data(Qt::UserRole + 1).isValid())
                head = r;
            else if (it->data(Qt::UserRole + 2).isValid()) {
                ++subs;
                hidden += int(it->isHidden());
            } else
                ++tops;
        }
        check(head >= 0 && subs == 3 && hidden == 3,
              u"a \"Safeguards\" row with 3 copies under the mod, folded shut (%1 rows, %2 hidden)"_s.arg(subs).arg(hidden));
        QStringList names;
        for (int r = 0; r < l->count(); ++r)
            if (!l->item(r)->data(Qt::UserRole + 2).isValid())
                names << l->item(r)->text().trimmed();
        check(tops == 1 && !names.join(u"|"_s).contains(u"(original"_s),
              u"the list of mods holds no copy: %1"_s.arg(names.join(u" | "_s)));
        if (head >= 0) {
            QListWidgetItem *cur = l->currentItem();
            const QPoint at = l->visualItemRect(l->item(head)).center();
            QMouseEvent press(QEvent::MouseButtonPress, at, l->viewport()->mapToGlobal(at), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(l->viewport(), &press);
            QApplication::processEvents();
            int shown = 0;
            for (int r = 0; r < l->count(); ++r)
                shown += int(l->item(r)->data(Qt::UserRole + 2).isValid() && !l->item(r)->isHidden());
            check(shown == 3 && l->currentItem() == cur && l->item(head)->text().contains(u"▾"_s),
                  u"a click on it folds it open; the chosen mod stays chosen (%1)"_s.arg(l->item(head)->text().trimmed()));
            l->setCurrentItem(l->item(head + 1));
            QApplication::processEvents();
            check(dlg.sc_hint->text().startsWith(u"SAFEGUARD of Test Mod"_s) && dlg.sc_hint->text().contains(u"SUBMOD of"_s),
                  u"a copy chosen: the hint says whose it is (a submod's copy: both)"_s);
            QApplication::sendEvent(l->viewport(), &press);
            QApplication::processEvents();
            check(l->item(head + 1)->isHidden() && l->currentItem() &&
                      !l->currentItem()->data(Qt::UserRole + 2).isValid() && !l->currentItem()->isHidden(),
                  u"folded shut over the chosen copy: its mod is chosen"_s);
        }
    }
    // a copy that can't be made stops the write
    const QString gone = pypath::join(mods, u"Gone Mod"_s);
    const int id2 = modcopy::arm(gone, QString(), [] { return true; }, [](const modcopy::Result &) {});
    bool threw = false;
    try {
        fs::watched(pypath::join(gone, u"a.txt"_s));
    } catch (const fs::OSError &) {
        threw = true;
    }
    check(threw, u"no copy possible: the write is refused (OSError)"_s);
    modcopy::disarm(id2);
    // a new mod from the base game: its folder with history/pops, a .mod without dependencies, its own user_dir
    {
        const auto [folder, modfile] = submods::create_new(mods, u"Fresh Mod"_s);
        const QByteArray t = get(modfile);
        check(fs::isdir(pypath::join(folder, {u"history"_s, u"pops"_s})), u"new mod: history/pops made"_s);
        check(t.contains("name = \"Fresh Mod\"") && t.contains("path = \"mod/Fresh Mod\"") &&
                  t.contains("user_dir = \"Fresh Mod\"") && !t.contains("dependencies"),
              u"new mod: its .mod (name, path, user_dir, no dependencies)"_s);
        bool twice = false;
        try {
            submods::create_new(mods, u"Fresh Mod"_s);
        } catch (const py::ValueError &) {
            twice = true;
        }
        check(twice, u"new mod: a name in use is refused"_s);
        bool bad = false;
        try {
            submods::create_new(mods, u"a/b"_s);
        } catch (const py::ValueError &) {
            bad = true;
        }
        check(bad, u"new mod: a name with / is refused"_s);
    }
    std::printf("modcopy: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
