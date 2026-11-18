// pospanel against Python, the C++ side of tools/check_pospanel.py.
// Usage: med_pospanel_check --dump <out folder> <work folder>   (MODEDITOR_SETTINGS names the settings file)
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/positions.h"
#include "core/pypath.h"
#include "ui/mapview.h"
#include "ui/pospanel.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::L;
using pyform::v;
using namespace pospanel;

static QString hs(const QString &s)
{
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
static QString vl(const QList<long long> &l) { return v(std::vector<long long>(l.begin(), l.end())); }
static QString tagv(const QVariantList &t)
{
    QStringList p;
    for (const QVariant &x : t)
        p << (x.typeId() == QMetaType::QString ? v(x.toString()) : v(x.toLongLong()));
    return pyform::list(p);
}
static QString rgbv(const std::tuple<int, int, int> &c)
{
    return u"[%1, %2, %3]"_s.arg(std::get<0>(c)).arg(std::get<1>(c)).arg(std::get<2>(c));
}

static int dump(const QString &out_dir, const QString &work)
{
    QDir().mkpath(out_dir);
    const QString BASE = pypath::join(work, u"Victoria 2"_s);
    auto m = std::make_shared<moddata::Mod>(pypath::join(work, u"Ferrum_Mare 1.2.000"_s), BASE, std::nullopt,
                                            QStringList{u"history"_s, u"events"_s, u"decisions"_s, u"localisation"_s,
                                                        u"gfx/flags"_s});
    m->load();
    auto mdp = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mapview::MapData &md = *mdp;
    mainwindow::MainWindow win(m, mdp, m->scenarios()[0].name, pypath::join(out_dir, u"backups"_s));
    win._check_timer->stop();
    win._rec_timer->stop();
    document::Document &doc = *win.doc;
    QStringList out, msgs, selects, zooms;
    PositionsPanel pp(*m, md);
    pp.view = win.map;
    QObject::connect(&pp, &PositionsPanel::message, [&](const QString &t) { msgs << t; });
    QObject::connect(&pp, &PositionsPanel::selectRequested,
                     [&](const QList<long long> &a, const QList<long long> &b) { selects << pyform::list({vl(a), vl(b)}); });
    QObject::connect(&pp, &PositionsPanel::zoomRequested, [&](double x, double y) { zooms << L(x, y); });
    QObject::connect(&pp, &PositionsPanel::overlayChanged, [&] { win.map->shape_layer->set_shapes(pp.shapes()); });
    pp.setFixedWidth(470);
    positions::PositionsFile &pf = doc.map<positions::PositionsFile>(u"positions"_s);
    PosEditHandler &hd = pp.handler;

    auto entries = [&] {
        std::vector<long long> ids;
        for (const auto &[pid, e] : pf.entries)
            ids.push_back(pid);
        std::sort(ids.begin(), ids.end());
        QStringList parts;
        for (long long pid : ids)
            parts << positions::fmt_entry(pid, **pf.entries.get(pid), u"\n"_s);
        return hs(parts.join(u'\n'));
    };
    auto shapes = [&] {
        QStringList sh;
        for (const mapview::Shape &s : pp.shapes()) {
            QStringList pts;
            for (const QPointF &q : s.pts)
                pts << L(q.x(), q.y());
            sh << u"%1 %2 %3 %4 %5 %6 %7 %8"_s.arg(s.kind, pyform::list(pts), rgbv(s.color), v(*s.size), tagv(*s.tag),
                                                   v(s.halo), v(s.min_scale), s.width ? v(*s.width) : u"None"_s);
        }
        QStringList names;
        for (const mapview::NameItem &n : pp.name_items())
            names << u"%1 %2 %3 %4 %5 %6 %7"_s.arg(v(n.cx), v(n.cy), v(n.angle), v(n.w), v(n.h), v(n.text), rgbv(n.rgb));
        return u"%1 %2 names %3 %4"_s.arg(sh.size()).arg(hs(sh.join(u'\n'))).arg(names.size()).arg(hs(names.join(u'\n')));
    };
    auto state = [&](const QString &what) {
        QStringList pr;
        for (const auto &[pid, lst] : pp._probs)
            for (const auto &[k, t] : lst)
                pr << u"%1:%2:%3"_s.arg(pid).arg(k, t);
        out << u"%1: probs %2 %3 entries %4"_s.arg(what).arg(pp._probs.size()).arg(hs(pr.join(u'|')), entries());
        out << u"  prob_info "_s + v(pp.prob_info->text());
        out << u"  fix %1 %2 next %3 regen_sel %4 help %5"_s.arg(v(pp.fix_naval->text()), v(pp.fix_naval->isEnabled()),
                                                               v(pp.next_prob->isEnabled()), v(pp.regen_sel->isEnabled()),
                                                               v(pp.edit_help->isVisibleTo(&pp)));
        out << u"  sel %1 title %2 info %3 section %4"_s.arg(v(pp.sel), v(pp.sel_title->text()), v(pp.sel_info->text()),
                                                            v(pp.sel_section->isEnabled()));
        out << u"  spins %1 %2 %3 %4"_s.arg(v(pp.scale_spin->value()), v(pp.scale_spin->isEnabled()),
                                           v(pp.rot_spin->value()), v(pp.rot_spin->isEnabled()));
        out << u"  shapes "_s + shapes();
        out << u"  msgs %1 selects %2 zooms %3"_s.arg(v(msgs), pyform::list(selects), pyform::list(zooms));
        msgs.clear();
        selects.clear();
        zooms.clear();
    };
    auto refresh = [&](const std::vector<long long> &sel) {
        pp.refresh(doc, sel, std::pair{u"Header %1"_s.arg(sel.size()), u"<b>sub</b>"_s});
    };
    auto entry_line = [&](long long pid) {
        return u"  entry %1 %2"_s.arg(pid).arg(v(positions::fmt_entry(pid, pf.get(pid), u"\n"_s)));
    };
    auto button = [&](const QString &text) {
        for (QPushButton *b : pp.findChildren<QPushButton *>())
            if (b->text() == text)
                return b;
        throw std::runtime_error("no button");
    };

    refresh({});
    state(u"start"_s);
    pp.show();
    QApplication::processEvents();
    pp.grab().save(QDir(out_dir).filePath(u"panel_start.png"_s));
    std::vector<long long> probs;
    for (const auto &[p, _l] : pp._probs)
        probs.push_back(p);
    std::sort(probs.begin(), probs.end());
    std::vector<long long> with_nb;
    for (long long p : pp.land)
        if (pf.get(p).pts.contains(u"naval_base"_s))
            with_nb.push_back(p);
    std::vector<long long> hist_nb(pp._hist_naval.begin(), pp._hist_naval.end());
    std::sort(hist_nb.begin(), hist_nb.end());
    auto first = [](const std::vector<long long> &x, size_t n) {
        return std::vector<long long>(x.begin(), x.begin() + qsizetype(std::min(n, x.size())));
    };
    out << u"picks probs %1 with_nb %2 hist_nb %3 land %4"_s.arg(v(first(probs, 6)), v(first(with_nb, 4)),
                                                                 v(first(hist_nb, 4)))
               .arg(pp.land.size());
    for (const QString &key : {u"names"_s, u"unit"_s, u"factory"_s})
        pp.show_boxes[key]->setChecked(false);
    state(u"hidden names, units, factories"_s);
    for (const QString &key : {u"names"_s, u"unit"_s, u"factory"_s})
        pp.show_boxes[key]->setChecked(true);
    const long long one = with_nb[0];
    refresh({one});
    state(u"one selected"_s);
    pp.show();
    QApplication::processEvents();
    pp.grab().save(QDir(out_dir).filePath(u"panel_one.png"_s));
    pp.scale_spin->setValue(1.7);
    refresh({one});
    state(u"name size 1.7"_s);
    out << entry_line(one);
    pp.rot_spin->setValue(-30.0);
    refresh({one});
    state(u"name angle -30"_s);
    out << entry_line(one);
    for (const QString &text : {u"Re-place name"_s, u"Re-place unit && buildings"_s, u"Re-place naval base"_s}) {
        button(text)->click();
        refresh({one});
        state(text);
        out << entry_line(one);
    }
    std::vector<long long> several = first(probs, 3);
    several.push_back(with_nb[1]);
    refresh(several);
    state(u"several selected"_s);
    pp.regen_sel->click();
    refresh(several);
    state(u"regenerate selection"_s);
    for (long long p : several)
        out << entry_line(p);
    pp.next_prob->click();
    pp.next_prob->click();
    state(u"next problem twice"_s);
    // --- moving markers
    pp.edit_btn->setChecked(true);
    refresh({one});
    state(u"edit mode"_s);
    const positions::Entry e = pf.get(one);
    for (const auto &[kind, dx, dy] : {std::tuple{u"unit"_s, 2.25, 1.5}, std::tuple{u"text_position"_s, -1.75, 0.5},
                                       std::tuple{u"naval_base"_s, 1.0, -2.0}}) {
        const auto [x, y] = pf.to_disp(*e.pts.get(kind));
        const bool got = hd.press(x, y, 1.0, Qt::NoModifier);
        hd.move(x + dx, y + dy);
        QStringList temp;
        for (const auto &s : win.map->shape_layer->temp) {
            QStringList pts;
            for (const QPointF &q : s.pts)
                pts << L(q.x(), q.y());
            temp << pyform::list({v(s.kind), pyform::list(pts)});
        }
        hd.release(x + dx, y + dy, 1.0, Qt::NoModifier);
        refresh({one});
        out << u"drag %1 pressed %2 temp %3"_s.arg(kind, v(got), pyform::list(temp));
        state(u"dragged "_s + kind);
        out << entry_line(one);
    }
    {
        const auto [x, y] = pf.to_disp(*pf.get(one).pts.get(u"unit"_s));
        hd.press(x, y, 1.0, Qt::NoModifier);
        hd.release(x + 0.1, y, 1.0, Qt::NoModifier);
    }
    state(u"click on a marker"_s);
    out << u"press nothing "_s + v(hd.press(0.5, 0.5, 1.0, Qt::NoModifier));
    out << u"cancel "_s + v(hd.cancel());
    std::vector<QVariantList> tags{{u"pos"_s, one, u"unit"_s}, {u"pos"_s, one, u"naval_base"_s},
                                   {u"pos"_s, probs[0], u"text_position"_s}};
    if (!pp._probs.isEmpty()) {
        std::vector<long long> ps;
        for (const auto &[p, _l] : pp._probs)
            ps.push_back(p);
        tags.push_back({u"posprob"_s, *std::min_element(ps.begin(), ps.end())});
    }
    for (const QVariantList &t : tags)
        out << u"describe %1 %2"_s.arg(tagv(t), v(pp.describe(t)));
    // --- naval bases
    refresh({with_nb[1]});                           // (selected: its markers show at any zoom)
    {
        const auto [x, y] = pf.to_disp(*pf.get(with_nb[1]).pts.get(u"naval_base"_s));
        out << u"rclick "_s + v(hd.rclick(x, y, 1.0, Qt::NoModifier));
    }
    refresh({one});
    state(u"rclick naval base"_s);
    refresh({hist_nb[0]});
    button(u"Remove naval base"_s)->click();
    refresh({hist_nb[0]});
    state(u"remove naval base (history)"_s);
    out << entry_line(hist_nb[0]);
    pp.fix_naval->click();
    refresh({});
    state(u"fix naval bases"_s);
    pp.edit_btn->setChecked(false);
    pp.regen_all->click();
    refresh({});
    state(u"regenerate all"_s);
    for (int k = 0; k < 4; ++k) {
        const auto u = doc.undo();
        out << u"undo "_s + (u ? v(*u) : u"None"_s);
    }
    refresh({});
    state(u"after undo"_s);
    QFile f(QDir(out_dir).filePath(u"lines.txt"_s));
    if (!f.open(QIODevice::WriteOnly))
        return 3;
    f.write((out.join(u'\n') + u'\n').toUtf8());
    std::printf("%d lines\n", int(out.size()));
    return 0;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
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
