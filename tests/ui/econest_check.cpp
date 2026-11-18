// Economy → Output estimate against Python, the C++ side of tools/check_econest.py.
// Usage: med_econest_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "../core/nopopup.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/pynum.h"
#include "core/pypath.h"
#include "map/mapview_data.h"
#include "ui/econest.h"
#include "ui/mapview.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;
using namespace econest;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString r(double x) { return py::repr_float(x); }
static QString amounts(const Amounts &d)
{
    QStringList parts;
    for (const auto &[k, v] : d)
        parts << k + u": "_s + r(v);
    return u"{"_s + parts.join(u", "_s) + u"}"_s;
}
static QString pick_s(const std::optional<Pick> &p)   // repr of Python's (level, unit) tuple
{
    if (!p)
        return u"None"_s;
    const QString u = p->first == u"province" ? p->second : u"'"_s + p->second + u"'"_s;
    return u"('%1', %2)"_s.arg(p->first, u);
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
    EconWorkspace ws(*m);
    ws.md = md.get();
    ws.resize(1500, 900);
    ws.show();
    QApplication::processEvents();
    ws.refresh(win.doc.get());
    QApplication::processEvents();

    const Estimate &d = *ws.data;
    for (const RgoRow &x : d.rgo)
        out << u"rgo %1|%2|%3|%4|%5|%6|%7|%8|%9"_s.arg(x.owner, x.state, QString::number(x.pid), x.province, x.good,
                                                       QString::number(x.workers), x.kinds.join(u'+'),
                                                       QString::number(x.size), r(x.base)) +
                   u" %1 %2 %3|%4|%5 %6"_s.arg(r(x.mult), r(x.thr), r(x.outb), x.terrain, r(x.out), r(x.money));
    for (const FacRow &x : d.factories)
        out << u"fac %1|%2|%3|%4|%5|%6|%7|%8|%9"_s.arg(x.owner, x.state, QString::number(x.pid), x.building,
                                                       QString::number(x.level), x.good, amounts(x.places),
                                                       amounts(x.got), r(x.full)) +
                   u" %1|%2|%3|%4|%5|%6 %7|%8 %9|"_s.arg(r(x.cap), amounts(x.need), x.local.join(u'+'), r(x.bonus),
                                                      amounts(x.idle), r(x.avail), x.shortg, r(x.out), amounts(x.inputs)) +
                   u"%1 %2 "_s.arg(r(x.money_out), r(x.money_in));
    for (const ArtRow &x : d.artisans)
        out << u"art %1|%2|%3|%4|%5|%6|%7|%8|%9"_s.arg(x.owner, x.state, QString::number(x.pid), x.province,
                                                       QString::number(x.people), QString::number(x.choices),
                                                       amounts(x.made), amounts(x.could), amounts(x.inputs)) +
                   u"|"_s + r(x.money);
    out << u"notes "_s + d.notes.join(u" / "_s);
    out << u"avail "_s + amounts(d.avail);
    out << u"demand "_s + amounts(d.demand);
    for (const auto &[g, t] : ws.tot) {
        QStringList pv, pp;
        for (const auto &[k, v] : t.prov)
            pv << QString::number(k) + u':' + r(v);
        for (const auto &[k, v] : t.p)
            pp << QString::number(k) + u':' + QStringList{r(v[0]), r(v[1]), r(v[2]), r(v[3])}.join(u',');
        out << u"tot %1 rgo %2 fac %3 art %4 use %5 art_use %6 prov %7 p %8"_s.arg(
            g, amounts(t.rgo), amounts(t.fac), amounts(t.art), amounts(t.use), amounts(t.art_use),
            h(pv.join(u';').toUtf8()), h(pp.join(u';').toUtf8()));
    }

    auto tree_rows = [&](QTreeWidget *t, const QString &tag, int limit) {
        QStringList rows;
        std::function<void(QTreeWidgetItem *, int)> walk = [&](QTreeWidgetItem *it, int depth) {
            QStringList cols;
            for (int c = 0; c < t->columnCount(); ++c) {
                const QString fg = it->foreground(c).style() != Qt::NoBrush ? it->foreground(c).color().name() : u"-"_s;
                cols << u"%1{%2%3}"_s.arg(it->text(c), fg, it->font(c).bold() ? u"B"_s : QString());
            }
            const QVariant u = it->data(0, Qt::UserRole + 1), lv = it->data(0, Qt::UserRole + 3);
            const QVariant sh = it->data(t == ws.rank ? 3 : 2, Qt::UserRole + 2);
            rows << u"%1 %2 | %3 %4 %5 | %6"_s.arg(QString::number(depth), cols.join(u" ¦ "_s),
                                                   u.isValid() ? u.toString() : QString(),
                                                   lv.isValid() ? lv.toString() : QString(),
                                                   sh.isValid() ? r(sh.toDouble()) : u"-"_s, it->toolTip(0));
            for (int i = 0; i < it->childCount(); ++i)
                walk(it->child(i), depth + 1);
        };
        for (int i = 0; i < t->topLevelItemCount(); ++i)
            walk(t->topLevelItem(i), 0);
        QStringList heads;
        for (int c = 0; c < t->columnCount(); ++c)
            heads << t->headerItem()->text(c) + (t->isColumnHidden(c) ? u"~"_s : QString());
        QTreeWidgetItem *cur = t->currentItem();
        out << u"%1 tree %2 rows %3 | head %4 | current %5"_s.arg(tag, QString::number(rows.size()),
                                                                   h(rows.join(u'\n').toUtf8()), heads.join(u" ¦ "_s),
                                                                   cur ? cur->text(0) : u"-"_s);
        for (qsizetype i = 0; i < rows.size() && i < limit; ++i)
            out << tag + u"   "_s + rows[i];
    };

    auto snap = [&](const QString &tag) {
        out << u"%1 | by %2 | group %3 | good %4 | focus %5 | sel %6"_s.arg(
            tag, ws.by, ws.group, ws.good.value_or(u"None"_s), pick_s(ws.focus), pick_s(ws._sel));
        const std::pair<const char *, QLabel *> labels[] = {
            {"focus_lbl", ws.focus_lbl}, {"summary", ws.summary}, {"goods_head", ws.goods_head}, {"g_title", ws.g_title},
            {"hover", ws.hover}, {"rank_head", ws.rank_head}, {"close_head", ws.close_head},
            {"close_text", ws.close_text}, {"notes", ws.notes}};
        for (const auto &[nm, w] : labels)
            out << u"%1 %2 %3 %4"_s.arg(tag, QString::fromLatin1(nm), w->isVisibleTo(&ws) ? u"True"_s : u"False"_s,
                                        w->text());
        out << u"%1 tips %2 | %3"_s.arg(tag, ws.rank_head->toolTip(), ws.focus_bar->isVisibleTo(&ws) ? u"True"_s : u"False"_s);
        QStringList cp;
        for (long long p : ws._close_pids)
            cp << QString::number(p);
        out << u"%1 close box %2 | pids %3"_s.arg(tag, ws.close_box->isVisibleTo(&ws) ? u"True"_s : u"False"_s,
                                                  h(cp.join(u',').toUtf8()));
        QStringList bs;
        for (const QString &k : {QString(), u"continent"_s, u"nation"_s, u"state"_s}) {   // (Python's dict order)
            QPushButton *b = ws.group_btn.at(k);
            bs << u"%1:%2%3"_s.arg(k.isEmpty() ? u"off"_s : k, b->isChecked() ? u"x"_s : QString(),
                                   b->isVisibleTo(&ws) ? QString() : u"~"_s);
        }
        out << tag + u" buttons "_s + bs.join(u' ');
        tree_rows(ws.goods, tag + u" goods"_s, 6);
        tree_rows(ws.rank, tag + u" rank"_s, 12);
        _EconMap *mp = ws.map;
        QByteArray lb;
        for (const auto &c : mp->lut)
            lb.append(reinterpret_cast<const char *>(c.data()), 3);
        QString edge = u"-"_s;
        if (mp->edge_ids) {
            QByteArray eb;
            for (long long v : *mp->edge_ids) {
                const std::int32_t x = static_cast<std::int32_t>(v);
                eb.append(reinterpret_cast<const char *>(&x), 4);
            }
            edge = h(eb);
        }
        QStringList pk;
        for (long long p : mp->picked)
            pk << QString::number(p);
        out << u"%1 map lut %2 edge %3 dark %4 | picked %5 %6 | legend %7 / %8 | view %9"_s.arg(
                   tag, mp->ids ? h(lb) : u"-"_s, edge, r(mp->edge_dark), QString::number(mp->picked.size()),
                   h(pk.join(u',').toUtf8()), ws.legend->mid, ws.legend->top, py::fmt_f(mp->scale, 3)) +
                   u" %1 %2"_s.arg(py::fmt_f(mp->ox, 3), py::fmt_f(mp->oy, 3));
        for (const auto &[x, y, t] : mp->extra)
            out << u"%1   extra %2 %3 %4"_s.arg(tag, r(x), r(y), t);
    };

    snap(u"start"_s);
    for (int i : {1, 2})
        if (QTreeWidgetItem *it = ws.goods->topLevelItem(i)) {
            ws.goods->setCurrentItem(it);
            QApplication::processEvents();
            snap(u"good %1"_s.arg(i));
        }
    for (const QString &k : {u"province"_s, u"state"_s, u"continent"_s, u"nation"_s}) {
        ws._set_by(k);
        QApplication::processEvents();
        snap(u"by "_s + k);
    }
    ws._set_by(u"state"_s);
    ws._set_group(u"nation"_s);
    QApplication::processEvents();
    snap(u"state grouped"_s);
    if (QTreeWidgetItem *top = ws.rank->topLevelItem(0); top && top->childCount()) {
        ws.rank->setCurrentItem(top->child(0));
        QApplication::processEvents();
        snap(u"state row picked"_s);
    }
    ws._set_group(QString());
    ws._set_by(u"nation"_s);
    QApplication::processEvents();
    if (QTreeWidgetItem *it = ws.rank->topLevelItem(1)) {
        ws.rank->setCurrentItem(it);
        QApplication::processEvents();
        snap(u"nation picked"_s);
        ws._rank_double(it);
        QApplication::processEvents();
        snap(u"nation focus"_s);
    }
    ws._set_by(u"province"_s);
    QApplication::processEvents();
    snap(u"focus by province"_s);
    ws.detach();
    QApplication::processEvents();
    snap(u"detached"_s);
    std::vector<long long> owned;
    for (const auto &[p, _o] : ws._own)
        if (!m->provinces.get(p)->sea)
            owned.push_back(p);
    std::sort(owned.begin(), owned.end());
    if (!owned.empty()) {
        const long long pid = owned.front();
        ws._map_double(int(pid));
        QApplication::processEvents();
        snap(u"map double %1"_s.arg(pid));
        ws._hover(int(pid));
        snap(u"hover"_s);
    }
    ws.rank->header()->setSortIndicator(0, Qt::AscendingOrder);
    QApplication::processEvents();
    snap(u"by name"_s);
    ws.detach();
    QApplication::processEvents();
    snap(u"esc"_s);
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
    QApplication::setCursorFlashTime(0);
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
