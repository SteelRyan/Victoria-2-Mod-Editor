// The main window on a SUBMOD against Python, the C++ side of tools/check_subwin.py: the submod strip and
// _submod_sync (after an Apply, for what a page wrote, for a file of the parent taken away in the working copy).
// Usage: med_subwin_check --run <side folder> <work folder>   (MODEDITOR_SETTINGS names the settings)
#include "../core/nopopup.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "core/submods.h"
#include "map/mapview_data.h"
#include "ui/changelog.h"
#include "ui/moreviews.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString tf(bool b) { return b ? u"True"_s : u"False"_s; }
static QString pyrepr(const QString &s)             // repr() of a text with neither quote nor backslash in it …
{
    if (s.contains(u'\'') && !s.contains(u'"'))
        return u"\""_s + s + u"\""_s;
    return u"'"_s + QString(s).replace(u"\\"_s, u"\\\\"_s).replace(u"'"_s, u"\\'"_s) + u"'"_s;
}

static QString listing(const QString &folder)
{
    // os.walk with the folders and files sorted: rel:size:hash, one after the other
    QStringList out;
    std::function<void(const QString &)> walk = [&](const QString &dir) {
        QDir d(dir);
        QStringList files = d.entryList(QDir::Files | QDir::Hidden | QDir::System);
        QStringList dirs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        std::sort(files.begin(), files.end());
        std::sort(dirs.begin(), dirs.end());
        for (const QString &fn : files) {
            const QString fp = pypath::join(dir, fn);
            QFile f(fp);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            out << u"%1:%2:%3"_s.arg(QDir(folder).relativeFilePath(fp)).arg(QFileInfo(fp).size()).arg(h(f.readAll()));
        }
        for (const QString &dn : dirs)
            walk(pypath::join(dir, dn));
    };
    walk(folder);
    return out.isEmpty() ? u"-"_s : out.join(u' ');
}

static int run(const QString &base, const QString &work_folder)
{
    const QString parent = pypath::join(base, u"parent"_s), sub = pypath::join(base, u"Checksub"_s),
                  work = pypath::join(base, u"work"_s);
    const QString backups = pypath::join(base, u"backups"_s), out_dir = pypath::join(base, u"out"_s);
    QDir().mkpath(out_dir);
    QStringList out;
    const QString BASE = pypath::join(work_folder, u"Victoria 2"_s);
    const auto info = submods::prepare({parent, sub}, work);
    auto m = std::make_shared<moddata::Mod>(work, BASE, u"Checksub"_s,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    auto s = std::make_shared<submods::Submod>();
    s->root = pypath::abspath(sub);
    s->name = u"Checksub"_s;
    s->parents = {u"Ferrum Mare"_s};
    s->parent_roots = {parent};
    s->view = info.view;
    s->state = info.state;
    s->work = work;
    m->submod = s;
    m->load();
    auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mainwindow::MainWindow win(m, md, m->scenarios()[0].name, backups);
    win.cl_ws->dir = QDir::toNativeSeparators(QDir(out_dir).filePath(u"changelog"_s));   // (as the Python side)
    auto quiet = [&] {
        win._check_timer->stop();
        win._rec_timer->stop();
        win._scan_timer->stop();
    };
    quiet();
    win.resize(1500, 900);
    win.show();
    QApplication::processEvents();
    quiet();
    auto st = [&](const QString &tag) {
        QApplication::processEvents();
        quiet();
        out << tag + u" | strip "_s + tf(win.sub_strip->isVisibleTo(&win)) + u" "_s + win.sub_strip->text();
        out << u"   msg "_s + win.st_msg->text() + u" | pending "_s + QString::number(win.doc->pending_count());
        out << u"   sub "_s + listing(sub);
    };
    st(u"open"_s);
    std::vector<long long> land;
    for (const auto &[p, pr] : m->provinces)
        if (!pr.sea)
            land.push_back(p);
    std::sort(land.begin(), land.end());
    win.on_name(u"PROV%1"_s.arg(land[3]), u"Subname"_s, std::nullopt);   // a text: the checksum stays the parent's
    st(u"name edited"_s);
    win.on_island(true, {land[9]});                                      // a province's history: it changes
    st(u"island edited"_s);
    win.save();
    st(u"applied"_s);
    {
        QFile f(pypath::join(pypath::join(work, u"events"_s), u"zz_subwin_page.txt"_s));
        if (f.open(QIODevice::WriteOnly))
            f.write("# written by a page without an Apply\n");
    }
    out << u"sync page | "_s + pyrepr(win._submod_sync());
    st(u"page wrote"_s);
    QStringList common = QDir(pypath::join(work, u"common"_s)).entryList({u"*.txt"_s}, QDir::Files);
    std::sort(common.begin(), common.end());
    const QString gone = common[0];
    QDir().rename(pypath::join(pypath::join(work, u"common"_s), gone), pypath::join(out_dir, gone));   // (kept in out)
    out << u"sync taken away "_s + gone + u" | "_s + pyrepr(win._submod_sync());
    st(u"taken away"_s);
    win.reload_note = u""_s;
    win._reload_mod();
    QApplication::processEvents();
    out << u"reload | %1 | visible %2 | note "_s.arg(tf(win.reload_requested), tf(win.isVisible())) + pyrepr(win.reload_note);
    QStringList keys;
    for (const auto &[k, _v] : m->submod->state)
        keys << k;
    std::sort(keys.begin(), keys.end());
    QStringList parts;
    for (const QString &k : keys) {
        const auto &v = *m->submod->state.get(k);
        parts << u"%1:%2:%3"_s.arg(k, std::get<0>(v)).arg(std::get<1>(v));
    }
    out << u"state %1 %2"_s.arg(keys.size()).arg(h(parts.join(u' ').toUtf8()));
    QFile f(pypath::join(out_dir, u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly))
        return 2;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    f.close();
    std::printf("%d lines\n", int(out.size()));
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 0);      // (as the Python side: no close, no recovery file)
    return 0;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    moreviews::app_dir_override = qEnvironmentVariable("PORT_APPDIR");   // (the library folder: never legacy\)
    QApplication::setCursorFlashTime(0);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--run")
            return run(a[2], a[3]);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 3;
    }
    std::fprintf(stderr, "usage: med_subwin_check --run <side folder> <work folder>\n");
    return 1;
}
