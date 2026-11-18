// mainwindow (phase 3) against Python, the C++ side of tools/check_mainwindow.py.
// Usage: med_mainwindow_check --dump <out folder> <work folder>   (MODEDITOR_SETTINGS names the settings file)
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "app/version.h"
#include "core/document.h"
#include "core/mapmodes.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "ui/checkpanel.h"
#include "ui/mapview.h"
#include "ui/modepanel.h"
#include "ui/moreviews.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QTimer>
#include <QTreeWidget>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::L;
using pyform::v;

static QString h(const void *p, size_t n)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(QByteArray::fromRawData(static_cast<const char *>(p), qsizetype(n)),
                                 QCryptographicHash::Sha1).toHex().left(16));
}

static QString norm(QString text)
{
    return text.replace(QRegularExpression(u"checked in [0-9.]+ s"_s), u"checked in 0.0 s"_s);
}

static QString var_v(const QVariant &x) { return x.isValid() && !x.isNull() ? v(x.toString()) : u"None"_s; }

static const QSet<QString> NOT_YET = {u"buildings"_s, u"filter"_s, u"events"_s, u"military"_s, u"soldiers"_s,
                                      u"status"_s, u"relvalues"_s, u"spheres"_s, u"subjects"_s, u"alliances"_s};
static const QStringList MODE_TABS = {u"pops"_s, u"provinces"_s, u"map"_s, u"nations"_s, u"errors"_s};
static constexpr int MAP_W = 1000, MAP_H = 700, SIDE_W = 470;

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    QStringList out;
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    QStringList times;
    QElapsedTimer clock;
    auto lap = [&](const char *what) {
        times << u"%1 %2"_s.arg(QString::fromLatin1(what)).arg(double(clock.nsecsElapsed()) / 1e9, 0, 'f', 3);
        clock.restart();
    };
    clock.start();
    auto m = std::make_shared<moddata::Mod>(pypath::join(work, u"Ferrum_Mare 1.2.000"_s), BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    lap("mod.load");
    auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    lap("MapData");
    const QString scen = m->scenarios()[0].name;
    clock.restart();
    mainwindow::MainWindow win(m, md, scen, pypath::join(out_dir, u"backups"_s));
    lap("MainWindow()");
    win._check_timer->stop();
    win._rec_timer->stop();
    win.resize(1500, 900);
    win.show();
    win.side_scroll->setFixedWidth(SIDE_W);
    win.map->setFixedSize(MAP_W, MAP_H);
    QApplication::processEvents();
    win._check_timer->stop();
    win.map->fit();
    QApplication::processEvents();

    auto box = [&] {
        QStringList r;
        auto *cb = win.color_box;
        auto *model = qobject_cast<QStandardItemModel *>(cb->model());
        for (int i = 0; i < cb->count(); ++i)
            r << u"[%1, %2, %3]"_s.arg(v(cb->itemText(i)), var_v(cb->itemData(i)), v(model->item(i)->isEnabled()));
        return pyform::list(r);
    };
    auto banner = [&] {
        QLabel *b = win.map->_banner;
        return b && b->isVisibleTo(win.map) ? b->text() : u""_s;
    };
    auto state = [&](const QString &tag) {
        out << u"%1 tab %2 page %3 mode %4 view %5"_s.arg(tag, win.tab, v(win.rail_page()), win.color_mode, win.view_mode);
        out << u"%1 box %2 current %3"_s.arg(tag, box()).arg(win.color_box->currentIndex());
        out << u"%1 pending %2 apply %3 revert %4 badge %5 %6 here %7 %8"_s.arg(
            tag, v(win.st_pending->text()), v(win.apply_btn->isEnabled()), v(win.revert_btn->isEnabled()),
            v(win.err_badge->isVisibleTo(&win)), v(win.err_badge->text()), v(win.here_btn->isVisibleTo(&win)),
            v(win.here_btn->text()));
        out << u"%1 banner %2"_s.arg(tag, v(banner()));
    };
    auto img = [&] { return h(win.map->_img32.data(), win.map->_img32.size() * 4); };

    // the version in the title is the C++ editor's own (the user 2026-11-15): left out to compare with Python
    out << u"title "_s + v(win.windowTitle().replace(QStringLiteral(" " MODEDITOR_VERSION_TEXT), QString()));
    QStringList sc;
    for (int i = 0; i < win.scenario_box->count(); ++i)
        sc << L(win.scenario_box->itemText(i), win.scenario_box->itemData(i).toString());
    out << u"scenarios %1 current %2"_s.arg(pyform::list(sc)).arg(win.scenario_box->currentIndex());
    state(u"start"_s);
    out << u"start image "_s + img();
    win.map->grab().save(QDir(out_dir).filePath(u"map_owner.png"_s));
    // --- the colour table of every map mode (pops tab)
    QStringList modes;
    for (const QString &t : MODE_TABS) {
        auto all = mainwindow::COLOR_MODES_BY_TAB.at(t);
        for (const auto &x : mapmodes::extra_modes(*m, t))
            all.push_back(x);
        for (const auto &[k, _t] : all)
            if (!modes.contains(k) && !NOT_YET.contains(k) && k != u"errors")
                modes << k;
    }
    const long long n = md->n;
    for (const QString &k : modes) {
        win.color_mode = k;
        const mapview::ColorTable t = win.color_table();
        const QString shape = t.Q > 1 ? L(n, (long long)t.Q, 3LL) : L(n, 3LL);
        out << u"table %1 %2 %3"_s.arg(k, shape, h(t.rgb.data(), t.rgb.size()));
    }
    win.color_mode = u"political"_s;
    win.render_map();
    // --- hover lines on the Owner map
    std::vector<long long> keys;
    for (const auto &[p, _pr] : m->provinces)
        keys.push_back(p);
    std::sort(keys.begin(), keys.end());
    std::vector<long long> land, sea;
    for (long long p : keys)
        if (p < n && md->area[size_t(p)] > 0)
            (m->provinces.get(p)->sea ? sea : land).push_back(p);
    std::vector<long long> picks;
    const size_t step = std::max<size_t>(1, land.size() / 6);
    for (size_t i = 0; i < land.size() && picks.size() < 6; i += step)
        picks.push_back(land[i]);
    picks.push_back(sea[0]);
    picks.push_back(sea[1]);
    for (long long p : picks) {
        const int x = int(md->cx[size_t(p)]), y = int(md->cy[size_t(p)]);
        win.on_hover(p, x, y);
        out << u"hover %1 %2 %3"_s.arg(p).arg(v(win.st_cursor->text()), v(win.st_hover->text()));
    }
    // --- clicks: a province, Shift (its state), Ctrl (one more), a sea tile, nothing
    for (const auto &[p, mods] : std::vector<std::pair<long long, int>>{
             {land[3], 0}, {land[3], 0x02000000}, {land[9], 0x04000000}, {sea[0], 0}, {0, 0}}) {
        win.on_click(p, Qt::KeyboardModifiers(mods));
        out << u"click %1 %2 sel %3 sea %4"_s.arg(p).arg(mods).arg(v(win.selection), v(win.sea_selection));
    }
    win.set_view_mode(u"state"_s);
    out << u"state view sel %1 image %2"_s.arg(v(win.selection), img());
    win.set_view_mode(u"province"_s);
    win.escape();
    // --- the Errors page
    win.set_tab(u"errors"_s);
    win._check_timer->stop();
    clock.restart();
    win._checks_due();
    lap("checks");
    QApplication::processEvents();
    win._check_timer->stop();
    auto *cp = win.chk_panel;
    cp->summary->setText(norm(cp->summary->text()));
    state(u"errors"_s);
    out << u"errors summary "_s + v(cp->summary->text());
    QStringList tree;
    for (int i = 0; i < cp->tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = cp->tree->topLevelItem(i);
        QStringList kids;
        for (int j = 0; j < std::min(3, it->childCount()); ++j)
            kids << it->child(j)->text(0);
        tree << L(it->text(0), (long long)it->childCount(), kids);
    }
    out << u"errors tree "_s + pyform::list(tree);
    std::vector<std::pair<QString, long long>> counts;
    for (const auto &[k, c] : cp->error_counts().first)
        counts.emplace_back(k.value_or(u"None"_s), c);
    std::sort(counts.begin(), counts.end());
    out << u"errors counts "_s + v(counts);
    std::vector<std::pair<QString, long long>> pend(win._pend_pages.begin(), win._pend_pages.end()),
        errp(win._err_pages.begin(), win._err_pages.end());
    out << u"errors pages %1 %2"_s.arg(v(pend), v(errp));
    out << u"errors image "_s + img();
    win.map->grab().save(QDir(out_dir).filePath(u"map_errors.png"_s));
    cp->grab().save(QDir(out_dir).filePath(u"errors_panel.png"_s));
    for (size_t i = 0; i < 4; ++i) {
        const long long p = picks[i];
        const int x = int(md->cx[size_t(p)]), y = int(md->cy[size_t(p)]);
        win.on_hover(p, x, y);
        out << u"errors hover %1 %2"_s.arg(p).arg(v(win.st_hover->text()));
    }
    // --- the first problem picked in the list
    if (cp->tree->topLevelItemCount() && cp->tree->topLevelItem(0)->childCount()) {
        cp->tree->setCurrentItem(cp->tree->topLevelItem(0)->child(0));
        QApplication::processEvents();
        out << u"picked %1 %2 banner %3"_s.arg(v(cp->sel_title->text()), v(cp->fix_btn->text()), v(banner()));
    }
    QFile f(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    f.close();
    QFile tf(QDir(out_dir).filePath(u"times.txt"_s));
    if (tf.open(QIODevice::WriteOnly))
        tf.write((times.join(u'\n') + u'\n').toUtf8());
    tf.close();
    std::printf("%d lines, 3 pictures\n", int(out.size()));
    std::fflush(stdout);
    win._rec_timer->stop();
    std::_Exit(0);                                 // (no close: nothing to keep — and no recovery file)
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    moreviews::app_dir_override = qEnvironmentVariable("PORT_APPDIR");   // (the library folder: never legacy\)
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 4 && a[1] == u"--dump")
            return dump(a[2], a[3]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
