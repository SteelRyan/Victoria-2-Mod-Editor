// How fast every page of the main window answers (C++ only, the user 2026-11-09: "do a performance update for every
// single window"). Ferrum Mare in the real main window, shown offscreen at 1500 × 900: every page is opened, then every
// map mode of it is picked, the map's sub-pages too, and a province is selected on the pages that show one. Each step
// is timed as the call + the events it leaves (the repaint); the longest freeze while the window's timers run
// afterwards is listed as "⟶ later freeze".
// Usage: med_pageperf_check <work folder> [rounds] [profile-step-substring]
//   (QT_QPA_PLATFORM=offscreen, MODEDITOR_SETTINGS of its own; a step whose name holds the third argument is profiled
//   with tests/core/sampler.h — build in build\pdb for the names)
#include "../core/nopopup.h"
#include "../core/sampler.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "ui/mapview.h"
#include "ui/modepanel.h"
#include "ui/theme.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <vector>

using namespace Qt::StringLiterals;

static std::map<QString, std::vector<double>> times;
static std::vector<QString> order;
static QString last_step;
static prof::Sampler *sampler = nullptr;
static QString profile_step;

static void flush()
{
    for (int i = 0; i < 3; ++i)
        QApplication::processEvents();
}

static void record(const QString &what, double ms)
{
    if (!times.count(what))
        order.push_back(what);
    times[what].push_back(ms);
}

static double timed(const QString &what, const std::function<void()> &fn)
{
    if (sampler && !profile_step.isEmpty() && what.contains(profile_step))
        sampler->on = true;
    QElapsedTimer t;
    t.start();
    fn();
    flush();
    const double ms = double(t.nsecsElapsed()) / 1e6;
    record(what, ms);
    last_step = what;
    return ms;
}

static void idle(int ms = 900)
{
    QElapsedTimer t;
    t.start();
    double worst = 0, busy = 0;
    while (t.elapsed() < ms) {
        QElapsedTimer one;
        one.start();
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        const double d = double(one.nsecsElapsed()) / 1e6;
        if (d > 2.0) {
            busy += d;
            worst = std::max(worst, d);
        }
        QThread::msleep(1);
    }
    if (!last_step.isEmpty() && worst > 20.0)       // (only freezes worth a look)
        record(last_step + u" ⟶ later freeze"_s, worst);
    if (sampler)
        sampler->on = false;
    last_step.clear();
}

static int run(const QString &work, int rounds)
{
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    auto m = std::make_shared<moddata::Mod>(pypath::join(work, u"Ferrum_Mare 1.2.000"_s), BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    auto mdp = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mapview::MapData &md = *mdp;
    mainwindow::MainWindow win(m, mdp, m->scenarios()[0].name, QDir::temp().filePath(u"pageperf_check_backups"_s));
    win.resize(1500, 900);
    win.show();
    flush();
    idle(1500);
    // a land province of a fair size with an owner
    long long A = 0;
    for (long long p = 1; p < md.n && !A; ++p)
        if (m->provinces.contains(p) && !m->provinces.get(p)->sea && md.area[size_t(p)] > 1500)
            A = p;
    std::printf("province %lld (%lld px), map %lld x %lld\n", A, (long long)md.area[size_t(A)], md.w, md.h);
    // the pages in the order of the rail
    const QStringList pages = {u"pops"_s,      u"provinces"_s, u"map"_s,       u"nations"_s,  u"armies"_s,
                               u"diplomacy"_s, u"events"_s,    u"technology"_s, u"production"_s, u"politics"_s,
                               u"warfare"_s,   u"econest"_s,   u"popinf"_s,    u"changelog"_s, u"library"_s,
                               u"import"_s,    u"mapsize"_s,   u"shortcuts"_s, u"submods"_s,   u"rules"_s,
                               u"errors"_s};
    QStringList walk = pages;                        // (PAGEPERF_PAGES=a,b: only those pages)
    if (qEnvironmentVariableIsSet("PAGEPERF_PAGES"))
        walk = qEnvironmentVariable("PAGEPERF_PAGES").split(u',', Qt::SkipEmptyParts);
    for (int r = 0; r < rounds; ++r) {
        for (const QString &pg : walk) {
            // (the first visit builds the page: listed apart)
            timed((r ? u"page again: "_s : u"page first: "_s) + pg, [&] { win.set_tab(pg); });
            idle();
            // every map mode (the list is the same on every map page: walked on the Provinces page only)
            const int n = pg == u"provinces" ? win.color_box->count() : 0;
            for (int i = 0; i < n; ++i) {
                const QString label = win.color_box->itemText(i);
                if (label.isEmpty())
                    continue;
                timed(u"mode: "_s + pg + u" · "_s + label, [&] { win.color_box->setCurrentIndex(i); });
                idle(500);
            }
            if (n > 0)
                win.color_box->setCurrentIndex(win.color_box->findText(u"Owner"_s, Qt::MatchContains));
            if (pg == u"provinces" || pg == u"pops" || pg == u"nations" || pg == u"map") {
                timed(u"select a province: "_s + pg, [&] { win.set_selection({A}); });
                idle(500);
                timed(u"select nothing: "_s + pg, [&] { win.set_selection({}); });
                idle(500);
            }
            if (pg == u"map")
                for (const QString &sub : {u"pos"_s, u"adj"_s, u"canal"_s, u"draw"_s, u"pos"_s}) {
                    timed(u"map sub-page: "_s + sub, [&] { win.set_map_sub(sub); });
                    idle(500);
                }
        }
        for (int k = 0; k < 4; ++k)
            timed(u"zoom step (Errors page)"_s, [&] { win.map->zoom_by(k % 2 ? 1.25 : 0.8, false); });
    }
    std::printf("\n%-60s %9s %9s %5s\n", "step", "median", "worst", "n");
    std::vector<std::pair<double, QString>> slow;
    for (const QString &k : order) {
        std::vector<double> v = times[k];
        std::sort(v.begin(), v.end());
        std::printf("%-60s %8.1f %8.1f %5d", qUtf8Printable(k.left(60)), v[v.size() / 2], v.back(), int(v.size()));
        if (v.size() > 1 && v.size() <= 6) {         // (each run in its order: a first one apart from the rest)
            std::printf("   [");
            for (double x : times[k])
                std::printf(" %.0f", x);
            std::printf(" ]");
        }
        std::printf("\n");
        slow.emplace_back(v[v.size() / 2], k);
    }
    std::sort(slow.begin(), slow.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    std::printf("\nslowest (median):\n");
    for (size_t i = 0; i < slow.size() && i < 25; ++i)
        std::printf("  %8.1f  %s\n", slow[i].first, qUtf8Printable(slow[i].second));
    if (sampler) {
        sampler->on = false;
        sampler->report(60, qUtf8Printable(profile_step));
    }
    return 0;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    const QStringList a = app.arguments();
    if (a.size() < 2) {
        std::printf("usage: med_pageperf_check <work folder> [rounds] [profile-step]\n");
        return 2;
    }
    std::unique_ptr<prof::Sampler> s;
    if (a.size() > 3) {
        profile_step = a[3];
        s = std::make_unique<prof::Sampler>(GetCurrentThread());
        sampler = s.get();
    }
    try {
        return run(a[1], a.size() > 2 ? a[2].toInt() : 1);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
}
