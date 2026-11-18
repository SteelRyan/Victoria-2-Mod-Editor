// app (the start window, the splash, the icon) against Python, the C++ side of tools/check_app.py.
// Usage: med_app_check --dump <out folder>   (MODEDITOR_SETTINGS names the settings file of the run)
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/app.h"
#include "core/pypath.h"
#include "core/settings.h"
#include "ui/theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::L;
using pyform::v;

static int dump(const QString &out_dir)
{
    const QString port = pypath::dirname(pypath::dirname(pypath::normpath(QCoreApplication::applicationDirPath())));
    const QString work = pypath::dirname(port);
    const QString mods = pypath::join(work, {u"Victoria 2"_s, u"mod"_s});
    QStringList out;
    out << u"has_mods "_s + L(app::has_mods(mods), app::has_mods(pypath::join(work, u"Victoria 2"_s)), app::has_mods(u""_s),
                              app::has_mods(pypath::join(work, u"nope"_s)));
    auto st = settings_::settings();
    app::OpenDialog dlg(*st, mods);
    auto rows = [&] {
        QStringList r;
        for (int i = 0; i < dlg.mod_list->count(); ++i) {
            QListWidgetItem *it = dlg.mod_list->item(i);
            r << L(it->text(), it->toolTip(), bool(it->flags() & Qt::ItemIsSelectable),
                   it->foreground().style() != Qt::NoBrush ? std::optional<QString>(it->foreground().color().name())
                                                           : std::nullopt);
        }
        return pyform::list(r);
    };
    auto state = [&] {
        QStringList sc;
        for (int i = 0; i < dlg.sc_list->count(); ++i)
            sc << dlg.sc_list->item(i)->text();
        return L(sc, dlg.sc_list->currentRow(), dlg.scenario(), dlg.ok_btn->isEnabled(), dlg.new_btn->isEnabled(),
                 dlg.sub_btn->isEnabled(), dlg.del_btn->isEnabled(), dlg.del_btn->text(), dlg.sc_hint->text(),
                 dlg.err->isVisibleTo(&dlg), dlg.err->text(),
                 dlg.mod ? std::optional<QString>(dlg.mod->root) : std::nullopt);
    };
    dlg.show();
    QApplication::processEvents();
    dlg.grab().save(QDir(out_dir).filePath(u"open.png"_s));
    out << u"rows "_s + rows();
    out << u"current %1 %2"_s.arg(dlg.mod_list->currentRow()).arg(state());
    for (int i = 0; i < dlg.mod_list->count(); ++i) {
        dlg.mod_list->setCurrentRow(i);
        out << u"pick %1 %2"_s.arg(i).arg(state());
    }
    dlg.mod_list->setCurrentRow(1);
    if (dlg.sc_list->count() > 1)
        dlg.sc_list->setCurrentRow(dlg.sc_list->count() - 1);
    QApplication::processEvents();
    dlg.grab().save(QDir(out_dir).filePath(u"open_picked.png"_s));
    dlg.try_accept();
    std::vector<std::pair<QString, QString>> kv;
    for (const QString &k : st->allKeys())
        kv.emplace_back(k, st->value(k).toString());
    std::sort(kv.begin(), kv.end());
    out << u"accepted %1 %2"_s.arg(dlg.result()).arg(v(kv));
    app::OpenDialog dlg2(*st, mods, u"⚠ Something went wrong before"_s);
    dlg2.show();
    QApplication::processEvents();
    dlg2.grab().save(QDir(out_dir).filePath(u"open_again.png"_s));
    out << u"again %1 %2 %3"_s.arg(dlg2.mod_list->currentRow()).arg(v(dlg2.sc_list->currentRow()), v(dlg2.scenario()));
    app::Splash sp;
    sp.show();
    sp.say(u"Reading Ferrum Mare…"_s);
    sp.grab().save(QDir(out_dir).filePath(u"splash.png"_s));
    app::make_icon().pixmap(64, 64).save(QDir(out_dir).filePath(u"icon.png"_s));
    QFile f(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    std::printf("%lld lines, %d pictures\n", static_cast<long long>(out.size()), 5);
    return 0;
}

int main(int argc, char *argv[])
{
    QApplication qapp(argc, argv);
    theme::apply(qapp);
    const QStringList a = qapp.arguments();
    try {
        if (a.size() >= 3 && a[1] == u"--dump")
            return dump(a[2]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
