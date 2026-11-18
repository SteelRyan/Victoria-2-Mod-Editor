// The Population page in the main window against Python, the C++ side of tools/check_popspage.py.
// Usage: med_popspage_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "ui/dialogs.h"
#include "ui/mapview.h"
#include "ui/panels.h"
#include "ui/popfixbox.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <algorithm>
#include <cstdio>
#include <set>

using namespace Qt::StringLiterals;
using pyform::v;

static QString h(const void *p, size_t n)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(QByteArray::fromRawData(static_cast<const char *>(p), qsizetype(n)),
                                 QCryptographicHash::Sha1)
            .toHex()
            .left(16));
}

static constexpr int MAP_W = 1000, MAP_H = 700, SIDE_W = 470;

static QString key(const std::optional<QString> &k) { return k ? *k : u"None"_s; }
static QString ckey(const QString &k) { return k == panels::NONE_KEY ? u"None"_s : k; }
static QString rgb(const widgets::Rgb &c)
{
    return u"%1,%2,%3"_s.arg(std::get<0>(c)).arg(std::get<1>(c)).arg(std::get<2>(c));
}

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
    win.map->fit();
    QApplication::processEvents();
    document::Document &d = *win.doc;
    panels::EditPanel *ep = win.edit_panel;
    panels::SplitPanel *sp = win.split_panel;

    auto chips = [](const widgets::ChipGroup *g) {
        QStringList r;
        for (const auto &[k, c] : g->boxes)
            r << u"%1|%2|%3|%4|%5"_s.arg(ckey(k), c->label, c->amount ? QString::number(*c->amount) : u"None"_s,
                                        v(c->narrowed), v(c->isChecked()));
        return r.join(u';');
    };
    auto pops = [&] {
        std::vector<long long> ids;
        for (const auto &[pid, _p] : d.pops)
            ids.push_back(pid);
        std::sort(ids.begin(), ids.end());
        QStringList lines;
        for (long long pid : ids)
            for (const moddata::Pop &p : *d.pops.get(pid))
                lines << u"%1 %2 %3 %4 %5"_s.arg(pid).arg(p.type, key(p.culture), key(p.religion)).arg(p.size);
        const QByteArray b = lines.join(u'\n').toUtf8();
        return h(b.data(), size_t(b.size()));
    };
    auto common = [&](const QString &tag) {
        const QWidget *cur = win.stack->currentWidget();
        out << u"%1 | mode %2 | page %3 | msg %4 | %5 | steps %6 | pops %7 | hl %8"_s.arg(
            tag, win.mode, cur == ep ? u"edit"_s : cur == sp ? u"split"_s : u"other"_s, v(win.st_msg->text()),
            win.st_pending->text(), QString::number(d.undo_stack.size()), pops(),
            h(win.map->hl.data(), win.map->hl.size()));
    };
    auto snap = [&](const QString &tag) {
        common(tag);
        out << u"%1 head %2 | %3 | %4 %5"_s.arg(tag, v(ep->title->text()), v(ep->sub->text()), v(ep->info->text()),
                                                v(ep->info->isVisibleTo(ep)));
        out << u"%1 types %2"_s.arg(tag, chips(ep->types));
        out << u"%1 cultures %2"_s.arg(tag, chips(ep->cultures));
        out << u"%1 religions %2"_s.arg(tag, chips(ep->religions));
        long long on = 0;
        for (QPushButton *b : ep->step_buttons)
            on += b->isEnabled();
        QStringList tools;
        for (const auto &[k, b] : ep->tool_btns)
            if (b->isEnabled())
                tools << k;
        out << u"%1 total %2 | %3 | steps on %4 | tools %5"_s.arg(tag, v(ep->total_edit->text()), v(ep->of_total->text()))
                   .arg(on)
                   .arg(tools.join(u','));
        QStringList segs;
        for (const widgets::Segment &s : *ep->mix_segments)
            segs << u"%1|%2|%3|%4"_s.arg(key(s.key), s.label, rgb(s.color), py::repr_float(s.pct));
        out << u"%1 mix %2 | %3"_s.arg(tag, ep->mix_field, segs.join(u';'));
        QStringList leg;
        for (const auto &[spin, val] : ep->mix_rows)
            leg << py::repr_float(spin->value()) + u"@"_s + val->text();
        out << u"%1 legend %2"_s.arg(tag, leg.join(u';'));
        QStringList outer;
        for (const widgets::Segment &o : ep->pie->outer)
            outer << py::repr_float(o.pct);
        out << u"%1 mixui %2 | apply %3 | ring %4 | zoom %5 %6 %7 | outer %8"_s.arg(
            tag, v(ep->mix_preview->text()), v(ep->mix_apply->isEnabled()), v(ep->mix_ring_note->isVisibleTo(ep)),
            v(ep->mix_zoom->isVisibleTo(ep)), py::repr_float(ep->mix_zoom->lo), py::repr_float(ep->mix_zoom->hi),
            outer.join(u';'));
        QStringList rows;
        for (int i = 0; i < ep->tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = ep->tree->topLevelItem(i);
            QStringList cols;
            for (int c = 0; c < 5; ++c)
                cols << it->text(c);
            rows << cols.join(u'|') + u"|"_s + it->foreground(0).color().name();
        }
        out << u"%1 tree %2"_s.arg(tag, rows.join(u';'));
        out << u"%1 state %2 | %3 | %4 | %5"_s.arg(tag, v(ep->state_section->isVisibleTo(ep)), v(ep->cap_label->text()),
                                                   v(ep->cap_btn->isEnabled()), v(ep->cons_btn->toolTip()));
        out << u"%1 split %2 %3 %4 | fix %5"_s.arg(tag, v(ep->split_btn->text()), v(ep->split_btn->isEnabled()),
                                                   v(ep->split_btn->toolTip()), v(win.pop_fix->isVisibleTo(ep)));
    };
    auto split_snap = [&](const QString &tag) {
        common(tag);
        out << u"%1 from %2 | %3 | warn %4 %5"_s.arg(tag, v(sp->src_label->text()), v(sp->moving->text()),
                                                     v(sp->warn->text()), v(sp->warn->isVisibleTo(sp)));
        QStringList ts;
        for (const widgets::Segment &t : *sp->targets)
            ts << u"%1|%2|%3|%4|%5"_s.arg(QString::number(t.pid), py::repr_float(t.pct), v(t.locked), t.label, rgb(t.color));
        out << u"%1 targets %2"_s.arg(tag, ts.join(u';'));
        QStringList rows, presets;
        for (const auto &[spin, amt] : sp->row_widgets)
            rows << amt->text() + u"@"_s + py::repr_float(spin->value());
        for (const auto &[k, b] : sp->preset_btns)
            if (b->isChecked())
                presets << k;
        out << u"%1 rows %2 | mode %3 | presets %4 | apply %5 | keep %6"_s.arg(
            tag, rows.join(u';'), sp->mode, presets.join(u','), v(sp->apply_btn->isEnabled()),
            v(sp->keep_cb->isChecked()));
    };
    auto accept_next = [] {
        QTimer::singleShot(0, [] {
            if (auto *dlg = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dlg->accept();
        });
    };
    auto total = [&](long long p) {
        long long s = 0;
        if (const auto *ps = d.pops.get(p))
            for (const auto &x : *ps)
                s += x.size;
        return s;
    };
    auto ncult = [&](long long p) {
        std::set<std::optional<QString>> cs;
        if (const auto *ps = d.pops.get(p))
            for (const auto &x : *ps)
                cs.insert(x.culture);
        return (long long)cs.size();
    };
    const Qt::KeyboardModifiers nomod = Qt::NoModifier;

    std::vector<long long> land;
    for (const auto &[p, pr] : m->provinces)
        if (!pr.sea && p < md->n && md->area[size_t(p)] > 0)
            land.push_back(p);
    std::sort(land.begin(), land.end());
    std::vector<long long> cands;
    for (long long p : land) {
        const moddata::Province &pr = *m->provinces.get(p);
        if (total(p) > 0 && pr.region && !pr.region->isEmpty() && m->regions.get(*pr.region)->provinces.size() >= 3)
            cands.push_back(p);
    }
    std::stable_sort(cands.begin(), cands.end(), [&](long long a, long long b) {
        const auto ka = std::make_tuple(-ncult(a), -total(a), a), kb = std::make_tuple(-ncult(b), -total(b), b);
        return ka < kb;
    });
    const long long src = cands[0];
    const QString rid = *m->provinces.get(src)->region;
    std::vector<long long> others;
    for (long long q : m->regions.get(rid)->provinces)
        if (q != src)
            others.push_back(q);
    out << u"source %1 region %2 others %3"_s.arg(src).arg(rid, v(others));
    win.set_tab(u"pops"_s);
    QApplication::processEvents();
    snap(u"start"_s);
    // --- one province
    win.on_click(src, nomod);
    QApplication::processEvents();
    snap(u"one"_s);
    ep->grab().save(QDir(out_dir).filePath(u"side_one.png"_s));
    // --- the step buttons, an exact total
    for (const QString &text : {u"+10%"_s, u"−.5%"_s, u"+.1%"_s}) {
        for (QPushButton *b : ep->step_buttons)
            if (b->text() == text) {
                b->click();
                break;
            }
        ep->mix_apply->click();                     // (C++ only: a step waits for "Apply changes" — deliberate)
        snap(u"step "_s + text);
    }
    ep->total_edit->setText(u"123,456"_s);
    Q_EMIT ep->total_edit->returnPressed();
    ep->mix_apply->click();                         // (C++ only, as above)
    snap(u"total"_s);
    ep->total_edit->setText(u"12a"_s);
    Q_EMIT ep->total_edit->returnPressed();
    snap(u"total refused"_s);
    // --- the filter
    const QString tkey = ep->types->boxes.begin()->first;
    (*ep->types->boxes.get(tkey))->setChecked(false);
    snap(u"no "_s + tkey);
    const QString ck = ep->cultures->boxes.begin()->first;
    (*ep->cultures->boxes.get(ck))->setChecked(false);
    snap(u"no "_s + ckey(ck));
    out << u"filter "_s + v(win.filter_description());
    win.color_mode = u"filter"_s;
    win.render_map();
    out << u"filter map "_s + h(win.map->_img32.data(), win.map->_img32.size() * 4);
    win.color_mode = u"political"_s;
    win.render_map();
    (*ep->cultures->boxes.get(ck))->setChecked(true);
    (*ep->types->boxes.get(tkey))->setChecked(true);
    snap(u"filter back"_s);
    // --- the composition editor
    (*ep->mix_btns.get(u"religion"_s))->click();
    snap(u"by religion"_s);
    (*ep->mix_btns.get(u"type"_s))->click();
    snap(u"by type"_s);
    (*ep->mix_btns.get(u"culture"_s))->click();
    snap(u"by culture"_s);
    if (!ep->mix_segments->empty()) {
        ep->_set_pct(0, 40.0);
        snap(u"share typed"_s);
    }
    {
        std::set<std::optional<QString>> have;
        for (const widgets::Segment &s : *ep->mix_segments)
            have.insert(s.key);
        QStringList cks;
        for (const auto &[k, _c] : m->cultures)
            cks << k;
        std::sort(cks.begin(), cks.end());
        QString nw;
        for (const QString &k : cks)
            if (!have.count(k)) {
                nw = k;
                break;
            }
        dialogs::PickCombo *cb = ep->mix_add_culture;
        cb->setCurrentIndex(cb->find(nw));
        Q_EMIT cb->activated(cb->currentIndex());
        snap(u"added "_s + nw);
    }
    ep->_set_pct(int(ep->mix_segments->size()) - 1, 10.0);
    snap(u"added share"_s);
    ep->grab().save(QDir(out_dir).filePath(u"side_mix.png"_s));
    ep->mix_apply->click();
    snap(u"mix applied"_s);
    // --- pop tools
    (*ep->tool_btns.get(u"copy"_s))->click();
    snap(u"copied"_s);
    win.on_click(others[0], nomod);
    accept_next();
    (*ep->tool_btns.get(u"paste"_s))->click();
    snap(u"pasted"_s);
    accept_next();
    (*ep->tool_btns.get(u"add"_s))->click();
    snap(u"added pop"_s);
    // --- split
    win.on_click(src, nomod);
    ep->split_btn->click();
    split_snap(u"split start"_s);
    win.on_click(others[0], nomod);
    win.on_click(others[1], nomod);
    split_snap(u"two targets"_s);
    for (const QString &k : {u"pop"_s, u"area"_s, u"even"_s}) {
        (*sp->preset_btns.get(k))->click();
        split_snap(u"preset "_s + k);
    }
    sp->row_widgets[0].first->setValue(30.0);
    split_snap(u"typed 30"_s);
    sp->keep_cb->setChecked(true);
    split_snap(u"keep"_s);
    win.on_click(src, nomod);                      // (the source clicked: the share kept goes again)
    split_snap(u"keep off"_s);
    sp->ph_spin->setValue(100000);
    split_snap(u"leave all"_s);
    sp->ph_spin->setValue(100);
    win.on_click(others[1], nomod);                // (clicked again: out)
    split_snap(u"one target"_s);
    win.on_click(others[1], nomod);
    split_snap(u"back in"_s);
    sp->grab().save(QDir(out_dir).filePath(u"side_split.png"_s));
    win.map->grab().save(QDir(out_dir).filePath(u"map_split.png"_s));
    sp->apply_btn->click();
    snap(u"split applied"_s);
    win.on_click(src, nomod);
    ep->split_btn->click();
    win.escape();
    snap(u"split cancelled"_s);
    // --- a whole state
    win.set_view_mode(u"state"_s);
    win.set_selection(m->regions.get(rid)->provinces);
    snap(u"state"_s);
    ep->cons_btn->click();
    snap(u"consolidated"_s);
    ep->cap_btn->click();
    common(u"capital pick"_s);
    win.on_click(others.back(), nomod);
    snap(u"capital picked"_s);
    ep->cap_btn->click();
    win.escape();
    snap(u"capital cancelled"_s);
    win.set_view_mode(u"province"_s);
    // --- undo
    out << u"undo "_s + v(win.undo());
    out << u"undo "_s + v(win.undo());
    snap(u"undone"_s);
    // --- G4
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

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    const QStringList a = app.arguments();
    try {
        if (a.size() >= 6 && a[1] == u"--run")
            return run(a[2], a[3], a[4], a[5]);
    } catch (const std::exception &ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 2;
}
