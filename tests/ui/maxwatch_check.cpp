// (C++ only, 2026-11-18) A tester: "the program is de-fullscreening itself during use sometimes". The real main window,
// maximised on a side screen (never the primary one), goes to every page of the rail; after each the window must still
// be maximised. Prints every page after which it is not (and its size then), and every window-state change as it
// happens. Needs a real screen: run WITHOUT QT_QPA_PLATFORM=offscreen, while nobody uses that screen.
// Usage: med_maxwatch_check --run <mod> <backups> <work folder>   (MODEDITOR_SETTINGS names the settings)
#include "../core/nopopup.h"
#include "app/mainwindow.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "map/mapview_data.h"
#include "ui/moreviews.h"
#include "ui/rail.h"
#include "ui/theme.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QScreen>
#include <QTimer>
#include <QWindowStateChangeEvent>
#include <cstdio>

using namespace Qt::StringLiterals;

static QString now_page = u"(start)"_s;

class Watch : public QObject {
public:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::WindowStateChange) {
            auto *w = static_cast<QWidget *>(o);
            std::printf("  state change on %s: maximised %d, size %dx%d\n", now_page.toUtf8().constData(),
                        int(w->isMaximized()), w->width(), w->height());
            std::fflush(stdout);
        }
        return false;
    }
};

static void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QApplication::processEvents(QEventLoop::AllEvents, 20);
}

static int run(const QString &root, const QString &backups, const QString &work)
{
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    auto m = std::make_shared<moddata::Mod>(root, BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    std::printf("loading %s\n", root.toUtf8().constData());
    m->load();
    std::printf("loaded\n");
    auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    std::printf("map read\n");
    mainwindow::MainWindow win(m, md, m->scenarios()[0].name, backups);
    std::printf("window made\n");
    QScreen *side = nullptr;
    for (QScreen *s : QGuiApplication::screens())
        if (s != QGuiApplication::primaryScreen())
            side = s;
    if (!side) {
        std::printf("no side screen: not run (the primary screen is the user's)\n");
        return 3;
    }
    win.move(side->availableGeometry().topLeft() + QPoint(40, 40));
    win.showMaximized();
    Watch watch;
    win.installEventFilter(&watch);
    pump(1500);
    std::printf("start: maximised %d, %dx%d on %s\n", int(win.isMaximized()), win.width(), win.height(),
                side->name().toUtf8().constData());
    int bad = 0;
    for (int round = 0; round < 2; ++round)
        for (const auto &[key, page] : rail::PAGES) {
            now_page = key;
            try {
                win.go_page(key);
            } catch (const std::exception &e) {
                std::printf("  %s: error %s\n", key.toUtf8().constData(), e.what());
            }
            pump(350);
            if (const QString dir = qEnvironmentVariable("MAXWATCH_SHOTS"); round == 0 && !dir.isEmpty())
                win.grab().save(dir + u"/"_s + key + u".png"_s);   // (a picture of every page, when asked for)
            if (round == 0)
                std::printf("  page %-14s minimum %4dx%4d\n", key.toUtf8().constData(), win.minimumSize().width(),
                            win.minimumSize().height());
            if (round == 0 && win.minimumSize().height() > 900) {    // the deepest widgets that need that height
                for (QWidget *c : win.findChildren<QWidget *>()) {
                    if (!c->isVisible() || c->minimumSizeHint().height() < 700)
                        continue;
                    bool deeper = false;
                    for (QWidget *k : c->findChildren<QWidget *>(Qt::FindDirectChildrenOnly))
                        if (k->isVisible() && k->minimumSizeHint().height() >= 700)
                            deeper = true;
                    if (!deeper)
                        std::printf("      needs %d: %s '%s' (parent %s)\n", c->minimumSizeHint().height(),
                                    c->metaObject()->className(), c->objectName().toUtf8().constData(),
                                    c->parentWidget() ? c->parentWidget()->metaObject()->className() : "-");
                }
            }
            if (!win.isMaximized()) {
                ++bad;
                std::printf("NOT MAXIMISED after page %s (round %d): %dx%d, minimum %dx%d\n", key.toUtf8().constData(),
                            round + 1, win.width(), win.height(), win.minimumSizeHint().width(),
                            win.minimumSizeHint().height());
                std::fflush(stdout);
                win.showMaximized();
                pump(400);
            }
        }
    std::printf("maxwatch: %s\n", bad ? qPrintable(u"%1 page(s) left the maximised state"_s.arg(bad)) : "PASS");
    return bad ? 1 : 0;
}

int main(int argc, char *argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    theme::apply(app);
    moreviews::app_dir_override = qEnvironmentVariable("PORT_APPDIR");
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 5 && a[1] == u"--run")
            return run(a[2], a[3], a[4]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    std::fprintf(stderr, "usage: med_maxwatch_check --run <mod> <backups> <work folder>\n");
    return 2;
}
