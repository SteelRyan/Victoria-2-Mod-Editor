// A new mod from the base game (C++ only, the user 2026-11-13): submods::create_new in build/newmod/mod, then opened
// in the real main window (offscreen) on the work area's vanilla game: start dates and pops come from the base game, a
// pops edit + Apply writes the province's pop file INTO THE NEW MOD (the base game's copy unchanged), and no "original"
// copy is made of a mod that was just created.
// Usage: med_newmod_check <work folder>   (QT_QPA_PLATFORM=offscreen, MODEDITOR_SETTINGS of its own)
#include "../core/nopopup.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/modcopy.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/submods.h"
#include "ui/mapview.h"

#include <QApplication>
#include <QCheckBox>
#include <QSettings>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <cstdio>

using namespace Qt::StringLiterals;

static int fails = 0;
static void check(bool ok, const QString &what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", qUtf8Printable(what));
    fails += int(!ok);
}

static QByteArray tree_hash(const QString &root)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    QStringList files;
    for (QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories); it.hasNext();)
        files << it.next();
    files.sort();
    for (const QString &f : files) {
        QFile x(f);
        if (x.open(QIODevice::ReadOnly)) {
            h.addData(f.toUtf8());
            h.addData(x.readAll());
        }
    }
    return h.result().toHex().left(12);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    if (argc < 2) {
        std::printf("usage: med_newmod_check <work folder>\n");
        return 2;
    }
    const QString work = QString::fromLocal8Bit(argv[1]);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    const QString scratch = pypath::join(work, u"ModEditor Cpp/build/newmod"_s);
    QDir(scratch).removeRecursively();           // (our own scratch folder, made fresh)
    const QString mods = pypath::join(scratch, u"mod"_s);
    QDir().mkpath(mods);
    const QByteArray base_pops = tree_hash(pypath::join(BASE, {u"history"_s, u"pops"_s}));

    const auto [folder, modfile] = submods::create_new(mods, u"My Mod"_s);
    modcopy::mark_copied(folder);                // (as the open window does)
    auto m = std::make_shared<moddata::Mod>(folder, BASE, u"My Mod"_s);
    m->load();
    const auto sc = m->scenarios();
    check(!sc.empty() && sc[0].files > 0, u"start dates from the base game: %1 (%2 pop files)"_s.arg(
                                              sc.empty() ? u"none"_s : sc[0].name).arg(sc.empty() ? 0 : sc[0].files));
    if (sc.empty())
        return 1;
    auto mdp = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    check(mdp->n > 1000, u"the base game's map (%1 provinces)"_s.arg(mdp->n));
    {
        mainwindow::MainWindow win(m, mdp, sc[0].name, pypath::join(scratch, u"backups"_s));
        win.resize(1400, 860);
        win.show();
        QApplication::processEvents();
        long long pid = 0;
        for (const auto &[p, pr] : m->provinces)
            if (!pr.sea && !win.doc->province_pops(p).empty()) {
                pid = p;
                break;
            }
        check(pid > 0, u"pops shown from the base game (province #%1)"_s.arg(pid));
        win.set_tab(u"pops"_s);
        win.set_selection({pid});
        QApplication::processEvents();
        long long before = 0;
        for (const auto &p : win.doc->province_pops(pid))
            before += p.size;
        win.doc->scale({pid}, document::PopFilter(), 10.0);
        const bool saved = win.save();
        QApplication::processEvents();
        check(saved && win.doc->pending_count() == 0, u"Apply wrote the edit"_s);
        QStringList inmod;
        for (QDirIterator it(pypath::join(folder, {u"history"_s, u"pops"_s}), QDir::Files, QDirIterator::Subdirectories);
             it.hasNext();)
            inmod << QDir(folder).relativeFilePath(it.next());
        check(inmod.size() == 1, u"one pop file now in the new mod: %1"_s.arg(inmod.join(u", "_s)));
        check(tree_hash(pypath::join(BASE, {u"history"_s, u"pops"_s})) == base_pops, u"the base game's pops unchanged"_s);
        long long after = 0;
        for (const auto &p : win.doc->province_pops(pid))
            after += p.size;
        check(after > before, u"the province holds the new pops (%1 → %2)"_s.arg(before).arg(after));
        // the almanac on Apply (C++ only, 2026-11-15): a check's fresh settings start with the tick off — none written;
        // ticked, the next Apply writes "<mod name> almanac.html" into the mod's own folder, and the tick is remembered
        const QString alm = pypath::join(folder, u"My Mod almanac.html"_s);
        check(win.almanac_cb && !win.almanac_cb->isChecked() && !QFile::exists(alm),
              u"tick off (a check's own settings): no almanac written"_s);
        win.almanac_cb->setChecked(true);
        check(win.settings->value(u"almanacOnApply"_s).toBool(), u"the tick is remembered in the settings"_s);
        win.doc->scale({pid}, document::PopFilter(), 2.0);
        const bool saved2 = win.save();
        QApplication::processEvents();
        QFile af(alm);
        const QByteArray html = af.open(QIODevice::ReadOnly) ? af.readAll() : QByteArray();
        check(saved2 && !html.isEmpty(), u"ticked: Apply wrote \"My Mod almanac.html\" (%1 bytes)"_s.arg(html.size()));
        check(html.count("<table") >= 5 && html.contains("My Mod"),
              u"…with its tables (%1) and the mod's name"_s.arg(html.count("<table")));
        QFile::copy(alm, pypath::join(scratch, u"almanac_seen.html"_s));
        win.doc->undo_stack.clear();             // (nothing left to ask about on close)
    }
    QStringList orig;
    for (const QString &n : QDir(mods).entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot))
        if (n.contains(u"(original"_s))
            orig << n;
    check(orig.isEmpty(), u"no \"original\" copy of a mod made this run"_s);
    std::printf("newmod: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
