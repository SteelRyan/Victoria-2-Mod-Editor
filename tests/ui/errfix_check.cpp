// The Errors tab's fixes in the main window against Python, the C++ side of tools/check_errfix.py.
// Usage: med_errfix_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/positions.h"
#include "core/pypath.h"
#include "map/mapsize.h"
#include "map/newprov.h"
#include "map/provdelete.h"
#include "ui/checkpanel.h"
#include "ui/drawpanel.h"
#include "ui/mapsize.h"
#include "ui/mapview.h"
#include "ui/moreviews.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTimer>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::v;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}

static const std::map<QString, QString> SKIP = {
    {u"renumber"_s, u"closes the window and reads the mod again"_s},
    {u"stray_mod"_s, u"moves a folder out of the mod"_s}};
static constexpr size_t MANY = 25;
static constexpr int MAP_W = 1000, MAP_H = 700, SIDE_W = 470;

static int run(const QString &root, const QString &backups, const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    QStringList out;
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    auto m = std::make_shared<moddata::Mod>(root, BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mainwindow::MainWindow win(m, md, m->scenarios()[0].name, backups);
    win._check_timer->stop();
    win._rec_timer->stop();
    win.cl_ws->dir = QDir::toNativeSeparators(QDir(out_dir).filePath(u"changelog"_s));
    // (no changelog in the port yet: Python's is pointed into its side's own folder and not compared)
    win.resize(1500, 900);
    win.show();
    win.side_scroll->setFixedWidth(SIDE_W);
    win.map->setFixedSize(MAP_W, MAP_H);
    QApplication::processEvents();
    win._check_timer->stop();
    document::Document &d = *win.doc;
    auto *dp = win.draw_panel;
    auto *cp = win.chk_panel;

    auto rec = [&](const QString &tag) {
        auto &pf = d.map<positions::PositionsFile>(u"positions"_s);
        std::vector<long long> ids;
        for (const auto &[p, _e] : pf.entries)
            ids.push_back(p);
        std::sort(ids.begin(), ids.end());
        QString all;
        for (long long p : ids)
            all += positions::fmt_entry(p, **pf.entries.get(p), u"\n"_s);
        const auto &idf = md->idmap.flat();
        const QByteArray idb(reinterpret_cast<const char *>(idf.data()), qsizetype(idf.size() * sizeof(idf[0])));
        out << u"%1 | msg %2 | %3 | idmap %4 | positions %5 | steps %6"_s.arg(
            tag, v(win.st_msg->text()), win.st_pending->text(), h(idb), h(all.toUtf8()),
            QString::number(d.undo_stack.size()));
    };
    auto cat = [&](const QString &key) -> std::pair<int, const mapcheck::Category *> {
        for (size_t i = 0; i < cp->cats.size(); ++i)
            if (cp->cats[i].key == key)
                return {int(i), &cp->cats[i]};
        return {-1, nullptr};
    };
    auto checks = [&] {
        win._check_timer->stop();
        cp->_sig.reset();
        win._checks_due();
        QApplication::processEvents();
        win._check_timer->stop();
    };
    auto fixable_of = [&](const mapcheck::Category &c) {
        std::vector<int> r;
        for (size_t i = 0; i < c.items.size(); ++i)
            if (checkpanel::MapCheckPanel::_fixable(c, c.items[i]))
                r.push_back(int(i));
        return r;
    };

    // --- planted: a new province, 40 of its pixels painted
    std::vector<long long> keys;
    for (const auto &[p, pr] : m->provinces)
        if (!pr.sea && p < md->n && md->area[size_t(p)] > 0)
            keys.push_back(p);
    std::sort(keys.begin(), keys.end());
    const long long l3 = keys[3];
    auto &defs = d.map<newprov::ProvinceDefs>(u"definitions"_s);
    const newprov::Plan pl = newprov::plan(d, defs, u"Neu Land"_s, false, l3);
    newprov::create_province(d, pl.pid, pl.rec, pl.rid, pl.continent, pl.climate, pl.want);
    {
        const auto &idm = md->idmap.flat();
        drawpanel::Flat idx;
        drawpanel::Ids old, nw;
        for (size_t i = 0; i < idm.size() && idx.size() < 40; ++i)
            if (idm[i] == l3) {
                idx.push_back(std::int64_t(i));
                old.push_back(idm[i]);
                nw.push_back(std::uint16_t(pl.pid));
            }
        dp->doc = &d;
        dp->commit(u"Paint Neu Land"_s, idx, old, nw);
    }
    win.set_tab(u"errors"_s);
    checks();
    rec(u"planted #%1"_s.arg(pl.pid));
    QStringList groups;
    for (const auto &c : cp->cats)
        if (c.fix && !c.items.empty()) {
            groups << c.key;
            out << u"group %1 %2 %3 items %4 fixable %5"_s.arg(c.key, c.fix->what, v(c.fix->label))
                       .arg(c.items.size())
                       .arg(fixable_of(c).size());
        }
    for (const QString &key : groups) {
        auto [ci, c] = cat(key);
        if (!c || c->items.empty()) {
            out << key + u": gone"_s;
            continue;
        }
        const QString what = c->fix->what;
        if (SKIP.count(what)) {
            out << u"%1: left out — %2"_s.arg(key, SKIP.at(what));
            continue;
        }
        const std::vector<int> fixable = fixable_of(*c);
        if (fixable.empty()) {
            out << key + u": nothing fixable"_s;
            continue;
        }
        cp->select_problem(ci, fixable[0]);
        cp->_fix(true);
        win._check_timer->stop();
        rec(key + u" one"_s);
        {
            const size_t steps = d.undo_stack.size();
            const std::optional<QString> u = steps ? win.undo() : std::nullopt;
            out << u"%1 undo %2"_s.arg(key, v(u));
        }
        checks();
        std::tie(ci, c) = cat(key);
        if (!c) {
            out << key + u": gone after the undo"_s;
            continue;
        }
        std::vector<const mapcheck::Item *> items;
        for (int i : fixable_of(*c))
            if (items.size() < MANY)
                items.push_back(&c->items[size_t(i)]);
        const size_t steps = d.undo_stack.size();
        const mapcheck::Category cc = *c;          // (the fix refreshes the list under it)
        std::vector<const mapcheck::Item *> its;
        for (const auto *it : items)
            its.push_back(&cc.items[size_t(it - c->items.data())]);
        try {
            cp->_do_fix(cc, its);
        } catch (const std::exception &e) {
            out << u"%1 many: raised %2"_s.arg(key, QString::fromUtf8(e.what()));
        }
        cp->_fix_note.reset();
        win._check_timer->stop();
        rec(u"%1 many (%2)"_s.arg(key).arg(items.size()));
        {
            const std::optional<QString> u = d.undo_stack.size() != steps ? win.undo() : std::nullopt;
            out << u"%1 undo %2"_s.arg(key, v(u));
        }
        checks();
    }
    // --- G4: "Fix this one" of every group, kept, then Apply
    for (const QString &key : groups) {
        auto [ci, c] = cat(key);
        if (!c || c->items.empty() || SKIP.count(c->fix->what))
            continue;
        const std::vector<int> fixable = fixable_of(*c);
        if (!fixable.empty()) {
            cp->select_problem(ci, fixable[0]);
            cp->_fix(true);
            win._check_timer->stop();
        }
    }
    rec(u"kept"_s);
    {
        const bool ok = win.save(false);
        out << u"apply %1 | msg %2 | %3"_s.arg(v(ok), v(win.st_msg->text()), win.st_pending->text());
    }
    QFile f(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    f.close();
    std::printf("%d lines\n", int(out.size()));
    std::fflush(stdout);
    win._rec_timer->stop();
    std::_Exit(0);                                 // (no close: nothing to keep — and no recovery file)
}

// (C++ only, 2026-11-08) a map cut by the older Map size (the tester's WICLBHSQP): positions off the map and colour maps
// still drawn for the whole map — both listed, both fixed by "Fix all", written on Apply, gone when read again.
// Usage: med_errfix_check --cutmap <copy of the mod> <backups> <work folder>
static int cutmap(const QString &root, const QString &backups, const QString &work)
{
    int fails = 0;
    auto check = [&](bool ok, const QString &what) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
        std::fflush(stdout);
        fails += !ok;
    };
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    auto m = std::make_shared<moddata::Mod>(root, BASE, std::nullopt, QStringList{});
    m->load();
    auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mainwindow::MainWindow win(m, md, m->scenarios()[0].name, backups);
    win._check_timer->stop();
    win._rec_timer->stop();
    win.resize(1500, 900);
    win.show();
    QApplication::processEvents();
    win._check_timer->stop();
    auto *cp = win.chk_panel;
    auto checks = [&] {
        win._check_timer->stop();
        cp->_sig.reset();
        win._checks_due();
        QApplication::processEvents();
        win._check_timer->stop();
    };
    auto cat = [&](const QString &key) -> const mapcheck::Category * {
        for (const auto &c : cp->cats)
            if (c.key == key)
                return &c;
        return nullptr;
    };
    win.set_tab(u"errors"_s);
    checks();
    for (const QString key : {u"pos_offmap"_s, u"colormap_size"_s}) {
        const mapcheck::Category *c = cat(key);
        check(c && !c->items.empty() && c->fix, u"%1 listed: %2 item(s)%3"_s.arg(key).arg(c ? c->items.size() : 0)
                                                    .arg(c && !c->items.empty() ? u" — first: "_s + c->items[0].text : u""_s));
        if (!c || c->items.empty() || !c->fix)
            continue;
        const mapcheck::Category cc = *c;
        std::vector<const mapcheck::Item *> its;
        for (const auto &it : cc.items)
            its.push_back(&it);
        cp->_do_fix(cc, its);
        win._check_timer->stop();
        std::printf("     message: %s\n", qPrintable(win.st_msg->text()));
        checks();
        const mapcheck::Category *after = cat(key);
        check(!after || after->items.empty(), u"%1 after Fix all: %2 left"_s.arg(key).arg(after ? after->items.size() : 0));
    }
    const bool ok = win.save(false);
    check(ok, u"Apply: "_s + win.st_msg->text());
    // read back from disk
    moddata::Mod m2(root, BASE, std::nullopt, QStringList{});
    m2.load();
    const auto off = mapsize::colormaps_off(m2, md->w, md->h);
    check(off.empty(), u"colour maps on disk fit the map (%1 off)"_s.arg(off.size()));
    QFile pf(m2.p({u"map"_s, u"positions.txt"_s}));
    long long bad = 0, n = 0;
    if (pf.open(QIODevice::ReadOnly)) {
        const QString t = QString::fromLatin1(pf.readAll());
        static const QRegularExpression PAIR(uR"(\bx\s*=\s*(-?[\d.]+)\s*y\s*=\s*(-?[\d.]+))"_s);
        for (auto it = PAIR.globalMatch(t); it.hasNext();) {
            const auto mm = it.next();
            const double x = mm.captured(1).toDouble(), y = mm.captured(2).toDouble();
            ++n;
            bad += !(0 <= x && x < double(md->w) && 0 <= y && y < double(md->h));
        }
    }
    check(n > 0 && bad == 0, u"positions.txt on disk: %1 points, %2 off the map"_s.arg(n).arg(bad));
    std::printf("%s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    std::fflush(stdout);
    win._rec_timer->stop();
    std::_Exit(fails ? 1 : 0);
}

// (C++ only, 2026-11-08) Map size's cut through the window, where whole provinces go (deleted, the highest numbers
// renumbered into their places, saved) before the cut: the cut must follow the numbers on disk. Afterwards: every
// position on the map, as many unit places on their own province as before, colour maps of the map's shape.
// Usage: med_errfix_check --cutflow <copy of a mod> <backups> <work folder> <cut index>
static double own_share(const QString &root, const QString &base, long long *n_out = nullptr)
{
    moddata::Mod m(root, base, std::nullopt, QStringList{});
    m.load();
    mapview::MapData md(m, m.p({u"map"_s, m.provinces_bmp}));
    positions::PositionsFile pf(m, md.h);
    long long n = 0, own = 0;
    for (const auto &[pid, e] : pf.entries) {
        if (!(0 < pid && pid < md.n && md.area[size_t(pid)]) || !e->pts.contains(u"unit"_s))
            continue;
        const auto d = pf.to_disp(*e->pts.get(u"unit"_s));
        const long long x = (long long)d.first, y = (long long)d.second;
        ++n;
        own += 0 <= x && x < md.w && 0 <= y && y < md.h && md.idmap(y, x) == pid;
    }
    if (n_out)
        *n_out = n;
    return n ? double(own) / double(n) : 0;
}

static int cutflow(const QString &root, const QString &backups, const QString &work, int k, long long want_l = -1,
                   long long want_r = -1)
{
    int fails = 0;
    auto check = [&](bool ok, const QString &what) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
        std::fflush(stdout);
        fails += !ok;
    };
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    long long n0 = 0;
    const double before = own_share(root, BASE, &n0);
    std::printf("before: %lld unit places, %.4f on their own province\n", n0, before);
    auto m = std::make_shared<moddata::Mod>(root, BASE, std::nullopt, QStringList{});
    m->load();
    auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    const long long w0 = md->w, n_prov0 = md->n;
    mainwindow::MainWindow win(m, md, m->scenarios()[0].name, backups);
    win._check_timer->stop();
    win._rec_timer->stop();
    win.resize(1500, 900);
    win.show();
    QApplication::processEvents();
    win._check_timer->stop();
    bool reloaded = false;
    QObject::connect(win.size_ws, &mapsize::MapSizeWorkspace::reloadRequested, [&] { reloaded = true; });
    win.set_tab(u"mapsize"_s);
    QApplication::processEvents();
    auto *P = win.size_ws;
    P->set_dir(u"w"_s);
    if (want_l >= 0) {                               // an exact cut: so many columns off the left / right end
        P->steps->setValue(want_l + want_r);
        QApplication::processEvents();
        P->left->setValue(want_l);
        QApplication::processEvents();
        check(P->total() == want_l + want_r && P->left->value() == want_l,
              u"cut set to %1 left + %2 right (page: %3 total, %4 left)"_s.arg(want_l).arg(want_r).arg(P->total())
                  .arg(P->left->value()));
    } else
        P->steps->setValue(P->_cuts[size_t(std::min<long long>(k, (long long)P->_cuts.size() - 1))]);
    QApplication::processEvents();
    const long long l = std::min(P->left->value(), P->total());
    const auto gone = mapsize::affected(*md, l, P->total() - l).first;
    std::printf("cut %lld columns (%lld left), %zu provinces wholly gone, highest province %lld\n", P->total(), l,
                gone.size(), n_prov0 - 1);
    check(!gone.empty(), u"the cut takes whole provinces (%1)"_s.arg(gone.size()));
    P->_cut();                                       // armed
    P->_cut();                                       // done
    QApplication::processEvents();
    std::printf("     message: %s\n", qPrintable(win.st_msg->text()));
    check(reloaded, u"the cut asked for the mod to be read again"_s);
    long long n1 = 0;
    const double after = own_share(root, BASE, &n1);
    std::printf("after: %lld unit places, %.4f on their own province\n", n1, after);
    check(after >= before - 0.02, u"unit places on their own province: %1 → %2"_s.arg(before, 0, 'f', 4).arg(after, 0, 'f', 4));
    moddata::Mod m2(root, BASE, std::nullopt, QStringList{});
    m2.load();
    mapview::MapData md2(m2, m2.p({u"map"_s, m2.provinces_bmp}));
    check(md2.w == w0 - P->total(), u"map now %1 wide (was %2)"_s.arg(md2.w).arg(w0));
    const auto off = mapsize::colormaps_off(m2, md2.w, md2.h);
    check(off.empty(), u"colour maps of the map's shape (%1 off)"_s.arg(off.size()));
    QFile pf(m2.p({u"map"_s, u"positions.txt"_s}));
    long long bad = 0, n = 0;
    if (pf.open(QIODevice::ReadOnly)) {
        const QString t = QString::fromLatin1(pf.readAll());
        static const QRegularExpression PAIR(uR"(\bx\s*=\s*(-?[\d.]+)\s*y\s*=\s*(-?[\d.]+))"_s);
        for (auto it = PAIR.globalMatch(t); it.hasNext();) {
            const auto mm = it.next();
            const double x = mm.captured(1).toDouble(), y = mm.captured(2).toDouble();
            ++n;
            bad += !(0 <= x && x < double(md2.w) && 0 <= y && y < double(md2.h));
        }
    }
    check(n > 0 && bad == 0, u"positions.txt: %1 points, %2 off the map"_s.arg(n).arg(bad));
    std::printf("%s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    std::fflush(stdout);
    win._rec_timer->stop();
    std::_Exit(fails ? 1 : 0);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    moreviews::app_dir_override = qEnvironmentVariable("PORT_APPDIR");   // (the library folder: never legacy\)
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 6 && a[1] == u"--run")
            return run(a[2], a[3], a[4], a[5]);
        if (a.size() >= 6 && a[1] == u"--cutflow")
            return a[5].contains(u':') ? cutflow(a[2], a[3], a[4], 0, a[5].section(u':', 0, 0).toLongLong(),
                                                 a[5].section(u':', 1, 1).toLongLong())
                                       : cutflow(a[2], a[3], a[4], a[5].toInt());
        if (a.size() >= 6 && a[1] == u"--fixall") {  // Fix all of the named groups (comma list), then Apply
            auto m = std::make_shared<moddata::Mod>(a[2], pypath::join(a[4], u"Victoria 2"_s), std::nullopt, QStringList{});
            m->load();
            auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
            mainwindow::MainWindow win(m, md, m->scenarios()[0].name, a[3]);
            win._check_timer->stop();
            win._rec_timer->stop();
            win.show();
            QApplication::processEvents();
            win.set_tab(u"errors"_s);
            auto checks = [&] {
                win._check_timer->stop();
                win.chk_panel->_sig.reset();
                win._checks_due();
                QApplication::processEvents();
                win._check_timer->stop();
            };
            checks();
            for (const QString &key : a[5].split(u',')) {
                const mapcheck::Category *c = nullptr;
                for (const auto &x : win.chk_panel->cats)
                    if (x.key == key)
                        c = &x;
                if (!c || c->items.empty() || !c->fix) {
                    std::printf("%s: nothing to fix\n", qPrintable(key));
                    continue;
                }
                const mapcheck::Category cc = *c;
                std::vector<const mapcheck::Item *> its;
                for (const auto &it : cc.items)
                    if (checkpanel::MapCheckPanel::_fixable(cc, it))
                        its.push_back(&it);
                win.chk_panel->_do_fix(cc, its);
                win._check_timer->stop();
                checks();
                size_t left = 0;
                for (const auto &x : win.chk_panel->cats)
                    if (x.key == key)
                        left = x.items.size();
                std::printf("%s: %zu fixed of %zu, %zu left — %s\n", qPrintable(key), its.size(), cc.items.size(), left,
                            qPrintable(win.st_msg->text()));
            }
            const bool ok = win.save(false);
            std::printf("apply %s: %s\n", ok ? "ok" : "FAILED", qPrintable(win.st_msg->text()));
            std::fflush(stdout);
            std::_Exit(ok ? 0 : 1);
        }
        if (a.size() >= 6 && a[1] == u"--delete") {  // Drawing → Delete province for the listed numbers, then Apply
            auto m = std::make_shared<moddata::Mod>(a[2], pypath::join(a[4], u"Victoria 2"_s), std::nullopt, QStringList{});
            m->load();
            auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
            mainwindow::MainWindow win(m, md, m->scenarios()[0].name, a[3]);
            win._check_timer->stop();
            win._rec_timer->stop();
            win.show();
            QApplication::processEvents();
            std::vector<long long> ids;
            for (const QString &x : a[5].split(u','))
                ids.push_back(x.toLongLong());
            const long long nd = provdelete::delete_provinces(*win.doc, *win.mapdata, ids, u"neighbours"_s,
                                                              win.landsea.get());
            std::printf("deleted %lld\n", nd);
            const bool ok = win.save(false);
            QApplication::processEvents();
            QStringList mv;
            for (const auto &[x, y] : win.renumbered)
                mv << u"%1>%2"_s.arg(x).arg(y);
            std::printf("apply %s: %s\nmoved %s\n", ok ? "ok" : "FAILED", qPrintable(win.st_msg->text()),
                        qPrintable(mv.join(u' ')));
            std::fflush(stdout);
            std::_Exit(ok ? 0 : 1);
        }
        if (a.size() >= 5 && a[1] == u"--list") {    // every Errors-tab group with problems, three items each
            auto m = std::make_shared<moddata::Mod>(a[2], pypath::join(a[4], u"Victoria 2"_s), std::nullopt, QStringList{});
            m->load();
            auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
            mainwindow::MainWindow win(m, md, m->scenarios()[0].name, a[3]);
            win._check_timer->stop();
            win._rec_timer->stop();
            win.show();
            QApplication::processEvents();
            win.set_tab(u"errors"_s);
            win.chk_panel->_sig.reset();
            win._checks_due();
            QApplication::processEvents();
            for (const auto &c : win.chk_panel->cats)
                if (!c.items.empty()) {
                    std::printf("[%s] %s (%zu) — %s\n", qPrintable(c.severity), qPrintable(c.key), c.items.size(),
                                qPrintable(c.title));
                    const bool all = qEnvironmentVariable("LIST_ALL").split(u',').contains(c.key);   // (every item)
                    for (size_t i = 0; i < c.items.size() && (all || i < 3); ++i)
                        std::printf("      %s\n", qPrintable(c.items[i].text.left(220)));
                }
            std::fflush(stdout);
            std::_Exit(0);
        }
        if (a.size() >= 6 && a[1] == u"--resize") {  // the Map size page as a user drives it: "w|h|gw|gh:A:B[:keep]"
            // (w / gw: A columns at the left (west), B at the right; h / gh: A rows at the top (north), B at the bottom;
            // "keep" unticks "delete the provinces that disappear")
            const QStringList sp = a[5].split(u':');
            auto m = std::make_shared<moddata::Mod>(a[2], pypath::join(a[4], u"Victoria 2"_s), std::nullopt, QStringList{});
            m->load();
            auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
            mainwindow::MainWindow win(m, md, m->scenarios()[0].name, a[3]);
            win._check_timer->stop();
            win._rec_timer->stop();
            win.resize(1500, 900);
            win.show();
            QApplication::processEvents();
            win._check_timer->stop();
            bool reloaded = false;
            QObject::connect(win.size_ws, &mapsize::MapSizeWorkspace::reloadRequested, [&] { reloaded = true; });
            win.set_tab(u"mapsize"_s);
            QApplication::processEvents();
            auto *P = win.size_ws;
            const QString d = sp.value(0);
            const long long A = sp.value(1).toLongLong(), B = sp.value(2).toLongLong();
            P->set_dir(d);
            QApplication::processEvents();
            if (sp.value(3) == u"keep"_s)
                P->del_box->setChecked(false);
            if (d.endsWith(u'h')) {
                P->h_steps->setValue(A + B);
                QApplication::processEvents();
                P->top->setValue(A);
            } else {
                P->steps->setValue(A + B);
                QApplication::processEvents();
                P->left->setValue(A);
            }
            QApplication::processEvents();
            const auto [no, so] = P->cut_rows();
            std::printf("map %lld x %lld; page set to %s: total %lld left %lld | rows north %lld south %lld | delete box %d\n",
                        md->w, md->h, qPrintable(d), P->total(), P->left->value(), no, so, int(P->del_box->isChecked()));
            std::printf("page says: %s\n", qPrintable(P->result->text().left(600)));
            P->_cut();                                   // armed
            P->_cut();                                   // done
            QApplication::processEvents();
            std::printf("warning: %s\n", P->warn->isVisible() ? qPrintable(P->warn->text()) : "(none)");
            std::printf("message: %s\nreloaded: %d\n", qPrintable(win.st_msg->text()), int(reloaded));
            std::fflush(stdout);
            win._rec_timer->stop();
            std::_Exit(reloaded ? 0 : 1);
        }
        if (a.size() >= 5 && a[1] == u"--cutmap")
            return cutmap(a[2], a[3], a[4]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
