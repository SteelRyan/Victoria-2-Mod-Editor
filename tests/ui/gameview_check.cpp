// gameview (the game's screen drawn by the editor, C++ only): renders views at the game's screen size, for comparing
// with shots of the real game at the same camera (x, z, h).
// Usage: med_gameview_check <mod root> <base or -> <out folder> x,z,h,t|p ...
#include "../core/nopopup.h"
#include "core/moddata.h"
#include "map/gamecam.h"
#include "map/mapview_data.h"
#include "ui/gameview.h"
#include "core/scriptstore.h"
#include "ui/locwin.h"
#include "ui/mapmarkspage.h"
#include "ui/shortcuts.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <cstdio>
#include <stdexcept>

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QStringList a = app.arguments();
    if (a.size() < 5) {
        std::printf("usage: med_gameview_check <mod root> <base or -> <out> x,z,h,t|p ...\n");
        return 2;
    }
    try {
        const std::optional<QString> base = a[2] == u"-" ? std::nullopt : std::optional<QString>(a[2]);
        auto m = std::make_shared<moddata::Mod>(a[1], base);
        m->load();
        const auto sc = m->scenarios();
        if (!sc.empty())
            m->apply_history(sc[0].name);
        mapview::MapData md(*m, m->p({u"map"_s, m->provinces_bmp}));
        QElapsedTimer t;
        t.start();
        auto world = gameview::make_world(*m, md, [&](long long pid) -> std::optional<QString> {
            const moddata::Province *pr = m->provinces.get(pid);
            return pr ? pr->owner : std::nullopt;
        });
        std::printf("world %lld ms, colormap %dx%d, water %dx%d, %lld countries\n", (long long)t.elapsed(),
                    world->land.width(), world->land.height(), world->water.width(), world->water.height(),
                    (long long)world->tags.size());
        QDir().mkpath(a[3]);
        // the 4K screen's picked-army windows as the editor draws them (unitpanel + the row of speed, recon …), 4 × big
        {
            scriptstore::ScriptStore store(*m);
            locwin::GameLook look(*m, &store);
            const std::pair<QString, QString> wins[2] = {{u"interface/unitpanel.gui"_s, u"unitpanel"_s},
                                                         {u"interface/single_unitpanel.gui"_s, u"sup_unit_status"_s}};
            for (const auto &[rel, win] : wins) {
                const shortcuts::Projected pr = shortcuts::project(look, rel, win, {}, true, true);
                if (pr.img)
                    pr.img->scaled(pr.img->size() * 4).save(QDir(a[3]).filePath(u"win_%1.png"_s.arg(win)));
                std::printf("window %s: %s\n", qPrintable(win), pr.img ? "drawn" : "not found");
            }
        }
        // (a few hand checks of the camera: the screen middle and back)
        gamecam::View v{2747, 1441, 330, 1600, 900};
        const auto mid = gamecam::to_map(v, 800, 450);
        const auto back = mid ? gamecam::to_screen(v, mid->x(), mid->y()) : std::nullopt;
        std::printf("middle of the screen at h330: map %.2f, %.2f (back %.3f, %.3f); px per map px %.3f\n",
                    mid ? mid->x() : -1, mid ? mid->y() : -1, back ? back->x() : -1, back ? back->y() : -1,
                    gamecam::px_per_unit(v, mid->x(), mid->y()));
        std::printf("wheel 330 in: %.4f, out: %.4f; 40 out x20: %.2f\n", gamecam::wheel(330, 1), gamecam::wheel(330, -1),
                    gamecam::wheel(40, -20));
        // against the game (reference/zoom 2026-11-09): 330 -> 297 -> 267.3 ... -> 40.12 -> 40; 40 -> 48 -> 57.6;
        // 330 -> 396 ... -> 985.4 -> 1000; a screen point back and forth
        int bad = 0;
        auto expect = [&](const char *what, double got, double want, double tol) {
            const bool ok = std::abs(got - want) <= tol;
            std::printf("  %-34s %10.3f  want %10.3f  %s\n", what, got, want, ok ? "ok" : "WRONG");
            bad += !ok;
        };
        expect("330, one notch in", gamecam::wheel(330, 1), 297.0, 1e-9);
        expect("330, two notches in", gamecam::wheel(330, 2), 267.3, 1e-9);
        double h = 330, last = 330;
        while (h > gamecam::MIN_H) {
            last = h;
            h = gamecam::wheel(h, 1);
        }
        expect("in to the end: the step before 40", last, 40.12, 0.005);
        expect("in to the end", h, 40.0, 1e-9);
        expect("40, one notch out", gamecam::wheel(40, -1), 48.0, 1e-9);
        expect("40, two notches out", gamecam::wheel(40, -2), 57.6, 1e-9);
        h = 330;                                         // (the zoom-out run of the game: 330, 396, 475, 570, 684, 821, 985)
        while (h < gamecam::MAX_H) {
            last = h;
            h = gamecam::wheel(h, -1);
        }
        expect("330 out: the step before 1000", last, 985.4, 0.05);
        expect("out to the end", h, 1000.0, 1e-9);
        expect("screen middle -> map -> screen x", back ? back->x() : -1, 800.0, 1e-6);
        expect("screen middle -> map -> screen y", back ? back->y() : -1, 450.0, 1e-6);
        const gamecam::Shown s5 = gamecam::shown(gamecam::Levels{}, 501), s4 = gamecam::shown(gamecam::Levels{}, 499);
        expect("counters at 499 / 501", double(s4.counters) - double(s5.counters), 1.0, 0);
        std::printf("camera: %s\n", bad ? "FAIL" : "PASS");
        if (bad)
            return 1;
        for (qsizetype i = 4; i < a.size(); ++i) {
            const QStringList p = a[i].split(u',');
            gameview::GameView gv;
            gv.md = &md;
            gv.world = world;
            // "s" for x: the view the 4K screen opens on (start_point), its country the player's
            const QPointF st = gameview::start_point(*world, *m, md);
            gv.cam.x = p[0] == u"s" ? st.x() : p[0].toDouble();
            gv.cam.z = p[0] == u"s" ? st.y() : p[1].toDouble();
            gv.cam.h = p[2].toDouble();
            gv.political = p.value(3) == u"p";
            gv.player = p[0] == u"s" ? gameview::owner_at(*world, md, st.x(), st.y() + 30.0)
                                     : gameview::owner_at(*world, md, 2747, 1441);
            gv.title = u"%1 %2"_s.arg(gv.political ? u"pol"_s : u"ter"_s).arg(p[2]);
            if (p.value(4) == u"u") {                  // (5th field "u") the 4K screen as Submods & 4K shows it
                static scriptstore::ScriptStore store(*m);
                static locwin::GameLook look(*m, &store);
                gv.screen = QSize(3840, 2160);
                double y = 150;
                for (gameview::Overlay &o : mapmarkspage::screen_windows(
                         look, {{u"interface/unitpanel.gui"_s, u"unitpanel"_s, 1.0, QPointF(0, y),
                                 {u"only_unit_from_selection_button"_s, u"disbandtoosmallbutton"_s}}})) {
                    y = o.y + o.img.height();
                    o.factor = 2;
                    gv.windows.push_back(o);
                }
                for (gameview::Overlay &o : mapmarkspage::screen_windows(
                         look, {{u"interface/single_unitpanel.gui"_s, u"sup_unit_status"_s, 1.0, QPointF(0, y + 18)},
                                {u"interface/topbar.gui"_s, u"topbar"_s, 1.0}})) {
                    o.factor = 2;
                    gv.windows.push_back(o);
                }
                gv.names_factor = 2;
                gv.counter_factor = 2;
                if (const auto parts = mapmarkspage::counter_parts(*m, &store)) {
                    auto pp = std::make_shared<mapmarkspage::Parts>(*parts);
                    gv.counter = [pp](QPainter &q) { mapmarkspage::draw_counter(q, *pp); };
                }
            }
            gv.setAttribute(Qt::WA_DontShowOnScreen);
            gv.resize(1600, 900);
            gv.show();
            QElapsedTimer tt;
            tt.start();
            const QImage img = gv.grab().toImage();
            const QString f = QDir(a[3]).filePath(u"view_%1.png"_s.arg(a[i]).replace(u',', u'_'));
            img.save(f);
            std::printf("%s: %lld ms\n", qPrintable(f), (long long)tt.elapsed());
        }
    } catch (const std::exception &e) {
        std::printf("ERROR %s\n", e.what());
        return 1;
    }
    return 0;
}
