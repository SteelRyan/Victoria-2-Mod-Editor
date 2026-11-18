// canalpanel against Python, the C++ side of tools/check_canalpanel.py.
// Usage: med_canalpanel_check --dump <out folder> <work folder>   (MODEDITOR_SETTINGS names the settings file)
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/document.h"
#include "core/localisation.h"
#include "core/moddata.h"
#include "core/pypath.h"
#include "ui/adjpanel.h"
#include "ui/canalpanel.h"
#include "ui/mapview.h"
#include "ui/theme.h"

#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <cstdio>

using namespace Qt::StringLiterals;
using pyform::v;
using namespace canalpanel;

static QString hs(const QString &s)
{
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
static QString tg(const std::optional<QVariantList> &t)
{
    if (!t)
        return u"None"_s;
    QStringList parts;
    for (const QVariant &x : *t)
        parts << x.toString();
    return parts.join(u"/"_s);
}
static QString pt(const std::optional<Pt> &p)
{
    return p ? v(p->first) + u","_s + v(p->second) : u"None"_s;
}
static QString rgb(const Rgb &c)
{
    const auto [r, g, b] = c;
    return u"[%1, %2, %3]"_s.arg(r).arg(g).arg(b);   // (golden_g2.v writes a tuple as a list)
}
static QString shp(const mapview::Shape &s)
{
    QStringList pts;
    for (const QPointF &p : s.pts)
        pts << pt(Pt{p.x(), p.y()});
    const auto [r, g, b] = s.color;
    const QString rect = s.kind == u"image"
                             ? QStringList{v(s.rect.x()), v(s.rect.y()), v(s.rect.width()), v(s.rect.height())}.join(u","_s)
                             : u"None"_s;
    return u"%1|%2|%3,%4,%5|"_s.arg(s.kind, pts.join(u";"_s)).arg(r).arg(g).arg(b) +
           u"%1|%2|%3|%4|%5|o%6|op%7|a%8|f%9|"_s.arg(s.width ? v(*s.width) : u"None"_s, s.size ? v(*s.size) : u"None"_s,
                                                    v(s.dash), v(s.halo), tg(s.tag), v(s.outline), v(s.opacity),
                                                    QString::number(s.alpha), s.frame ? rgb(*s.frame) : u"None"_s) +
           u"r%1|p%2|i%3"_s.arg(rect)
               .arg(s.polys.size())
               .arg(s.img.isNull() ? u"None"_s : u"%1x%2"_s.arg(s.img.width()).arg(s.img.height()));
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
    adjacency::AdjGeometry &geo = *win.geo;
    QStringList out, msgs, sigs;
    CanalPanel cp(*m, md, geo);
    cp.view = win.map;
    CanalHandler &hd = cp.handler;
    QObject::connect(&cp, &CanalPanel::message, [&](const QString &t) { msgs << t; });
    QObject::connect(&cp, &CanalPanel::zoomRequested, [&](double x, double y) { sigs << u"zoom %1 %2"_s.arg(v(x), v(y)); });
    int over = 0;
    QObject::connect(&cp, &CanalPanel::overlayChanged, [&] { ++over; });
    cp.setFixedWidth(470);
    auto &st = doc.map<scriptstore::ScriptStore>(u"scripts"_s);
    auto &loc = doc.map<localisation::LocEdits>(u"loc"_s);
    auto adj = [&]() -> adjacency::AdjacencyFile & { return doc.map<adjacency::AdjacencyFile>(u"adjacency"_s); };
    const QStringList SPRITES{u"map/province_flag_sprites/1_Canal.txt"_s, u"map/province_flag_sprites/2_canal.txt"_s,
                              u"map/province_flag_sprites/canal_33.txt"_s};

    auto vis = [&](QWidget *w) { return v(w->isVisibleTo(&cp)); };
    auto state = [&](const QString &what) {
        out << u"%1: sel %2 pick %3 rows %4 overlay %5"_s.arg(what, v(cp.sel_num), v(cp.pick))
                   .arg(adj().rows.size())
                   .arg(over);
        over = 0;
        out << u"  count %1 note %2 %3"_s.arg(v(cp.count->text()), v(cp.pick_note->text()), vis(cp.pick_note));
        QStringList rows;
        for (int i = 0; i < cp.tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = cp.tree->topLevelItem(i);
            QStringList cols;
            for (int c = 0; c < 4; ++c)
                cols << it->text(c);
            rows << u"%1|%2|%3|%4"_s.arg(cols.join(u"|"_s), it->toolTip(1), it->foreground(0).color().name(),
                                         v(it->isSelected()));
        }
        QStringList ends4 = rows.mid(0, 2);
        ends4 << rows.mid(std::max<qsizetype>(rows.size() - 2, 0));
        out << u"  tree %1 %2 %3 current %4"_s.arg(rows.size()).arg(
            hs(rows.join(u"\n"_s)), v(ends4),
            cp.tree->currentItem() ? v(cp.tree->currentItem()->text(0)) : u"None"_s);
        out << u"  editor %1 %2 %3 name %4 prov %5 %6 a %7 %8 b %9 %10"_s.arg(
            v(cp.ed_section->isEnabled()), v(cp.ed_title->text()), v(cp.ed_probs->text()), v(cp.ed_name->text()),
            QString::number(cp.ed_prov->value()), v(cp.ed_prov->name->text()), QString::number(cp.ed_a->value()),
            v(cp.ed_a->name->text()), QString::number(cp.ed_b->value()))
                   .arg(v(cp.ed_b->name->text()));
        QStringList mb;
        for (const QString &n : cp.model_order) {
            QPushButton *b = cp.model_btns[n];
            mb << u"%1:%2:%3:%4"_s.arg(b->text(), v(b->isChecked()), v(b->isEnabled()), b->toolTip());
        }
        out << u"  models %1 note %2"_s.arg(v(mb), v(cp.model_note->text()));
        out << u"  fit %1 cmp %2 %3 away %4"_s.arg(v(cp.fit_lbl->text()), v(cp.cmp_btn->isEnabled()), vis(cp.cmp_host),
                                                   v(cp.away_btn->isEnabled()));
        QStringList nd;
        for (const QString &k : {u"up"_s, u"left"_s, u"right"_s, u"down"_s})
            nd << v(cp.nudge[k]->isEnabled());
        out << u"  xy %1 %2 %3 %4 nudge %5"_s.arg(v(cp.ed_x->text()), v(cp.ed_x->isEnabled()), v(cp.ed_y->text()),
                                                 v(cp.ed_y->isEnabled()), pyform::list(nd));
        out << u"  open %1 %2 %3"_s.arg(v(cp.ed_open->isEnabled()), v(cp.ed_open->currentData().toString()),
                                        v(cp.open_note->text()));
        out << u"  decisions "_s + v(st.text(canals::OWN_DECISIONS));
        out << u"  events "_s + v(st.text(canals::OWN_EVENTS));
        for (const QString &rel : SPRITES)
            out << u"  sprite %1 %2"_s.arg(rel, v(st.text(rel)));
        QStringList lc;
        for (const auto &[k, val] : loc.fresh)
            lc << u"%1=(%2, %3)"_s.arg(k, v(val.first), v(val.second));
        out << u"  loc "_s + v(lc);
        QStringList cr;
        for (const adjacency::Row &r : adj().rows)
            if (r.type == u"canal" && (r.data == 1 || r.data == 2 || r.data == 33))
                cr << u"%1;%2;%3;%4;%5"_s.arg(r.a).arg(r.b).arg(r.through).arg(r.data).arg(r.comment);
        out << u"  canal rows "_s + v(cr);
        QStringList sh;
        for (const auto &s : cp.shapes())
            sh << shp(s);
        QStringList se = sh.mid(0, 2);
        se << sh.mid(std::max<qsizetype>(sh.size() - 2, 0));
        out << u"  shapes %1 %2 %3"_s.arg(sh.size()).arg(hs(sh.join(u"\n"_s)), v(se));
        out << u"  msgs %1 sigs %2"_s.arg(v(msgs), v(sigs));
        msgs.clear();
        sigs.clear();
    };
    auto refresh = [&] { cp.refresh(doc, {}, std::nullopt); };
    auto grab = [&](const QString &name) {
        cp.show();
        QApplication::processEvents();
        cp.grab().save(QDir(out_dir).filePath(name));
    };
    auto inside = [&](const canals::Canal &c) -> std::optional<Pt> {
        // The first point (pixel middles, row by row) inside the canal's outline.
        const auto path = cp.outline_path(c);
        if (!path)
            return std::nullopt;
        const QRectF br = path->boundingRect();
        for (double y = double((long long)br.top()) + 0.5; y < br.bottom(); y += 1.0)
            for (double x = double((long long)br.left()) + 0.5; x < br.right(); x += 1.0)
                if (path->contains(QPointF(x, y)))
                    return Pt{x, y};
        return std::nullopt;
    };
    auto pair_v = [](const std::optional<std::pair<long long, long long>> &p) {
        return p ? u"[%1, %2]"_s.arg(p->first).arg(p->second) : u"None"_s;
    };

    refresh();
    state(u"start"_s);
    grab(u"panel_start.png"_s);
    for (long long pid : {906LL, 2442LL, 364LL, 1563LL, 106LL, 3313LL, 0LL})
        out << u"anchor %1 %2 sea_pair %3"_s.arg(pid).arg(pt(cp.anchor(pid)), pair_v(cp.sea_pair(pid)));
    // --- canal 1, with a model
    cp.tree->setCurrentItem(cp.tree->topLevelItem(0));
    state(u"canal 1 from the list"_s);
    grab(u"panel_canal1.png"_s);
    {
        const canals::Canal c1 = *cp.canal(1);
        const auto pl = *cp.place(c1);
        out << u"place %1 %2 %3 %4"_s.arg(pt(pl.a), pl.model, v(pl.x), v(pl.y));
        for (const auto &[x, y] : std::vector<Pt>{{0.0, 0.0}, {pl.x, pl.y}, {3.0, -2.0}, {40.0, 40.0}}) {
            const auto f = cp.fit(c1, u"Kiel_Canal"_s, x, y);
            out << u"fit %1 %2 %3"_s.arg(v(x), v(y), v(cp.fit_text(f, c1)));
        }
    }
    const auto pl0 = *cp.place(*cp.canal(1));
    cp._model_picked(u"Panama_Canal"_s);
    refresh();
    state(u"model Panama"_s);
    for (const QString &key : {u"right"_s, u"up"_s, u"up"_s, u"left"_s, u"down"_s}) {
        cp.nudge[key]->click();
        refresh();
    }
    state(u"nudged"_s);
    cp.ed_x->setText(u"1,5"_s);
    cp.ed_y->setText(u" -2.25 "_s);
    cp._xy_typed();
    refresh();
    state(u"x / y typed"_s);
    cp.ed_x->setText(u"abc"_s);
    cp._xy_typed();
    state(u"bad x"_s);
    cp.move_to(1.5, -2.25);                          // (the same spot: no edit — the undo list at the end shows it)
    // --- compare the models
    cp._compare();
    std::vector<QToolButton *> btns;
    for (int i = 0; i < cp.cmp_row->count(); ++i)
        btns.push_back(qobject_cast<QToolButton *>(cp.cmp_row->itemAt(i)->widget()));
    for (size_t i = 0; i < btns.size(); ++i) {
        QToolButton *b = btns[i];
        out << u"compare %1 %2 %3 icon %4x%5"_s.arg(v(b->text()), v(b->toolTip()), v(b->isEnabled()))
                   .arg(b->iconSize().width())
                   .arg(b->iconSize().height());
        if (!b->icon().isNull())
            b->icon().pixmap(b->iconSize()).toImage().save(QDir(out_dir).filePath(u"compare_%1.png"_s.arg(i)));
    }
    state(u"compared"_s);
    grab(u"panel_compare.png"_s);
    for (QToolButton *b : btns)
        if (b->isEnabled() && b->text().contains(u"Suez"_s))
            b->click();
    refresh();
    state(u"Suez taken"_s);
    // --- placed elsewhere, dragged
    cp.away_btn->click();
    state(u"place armed"_s);
    out << u"press place "_s + v(hd.press(1000.5, 800.5, 1.0, Qt::NoModifier));
    refresh();
    state(u"placed elsewhere"_s);
    cp.move_to(pl0.x, pl0.y, u"back"_s);
    refresh();
    {
        auto p = *inside(*cp.canal(1));
        out << u"inside "_s + pt(p);
        const bool got = hd.press(p.first, p.second, 1.0, Qt::NoModifier);
        out << u"press model %1 drag %2"_s.arg(v(got), hd.drag ? u"[%1, %2, %3]"_s.arg(std::get<0>(*hd.drag))
                                                                    .arg(v(std::get<1>(*hd.drag)), v(std::get<2>(*hd.drag)))
                                                              : u"None"_s);
        hd.move(p.first + 2, p.second + 1);
        hd.move(p.first + 4.25, p.second - 3.5);
        out << u"temp %1"_s.arg(win.map->shape_layer->temp.size());
        hd.release(p.first + 4.25, p.second - 3.5, 1.0, Qt::NoModifier);
        refresh();
        state(u"dragged"_s);
        p = *inside(*cp.canal(1));
        hd.press(p.first, p.second, 1.0, Qt::NoModifier);
        hd.release(p.first + 0.1, p.second, 1.0, Qt::NoModifier);
        refresh();
        state(u"clicked, not dragged"_s);
    }
    out << u"press nothing "_s + v(hd.press(5.5, 5.5, 1.0, Qt::NoModifier));
    out << u"rclick "_s + v(hd.rclick(5.5, 5.5, 1.0, Qt::NoModifier));
    // --- a new canal by a click
    cp.new_btn->setChecked(true);
    state(u"new armed"_s);
    const Pt i1563 = *geo.inner(1563);
    out << u"press new "_s + v(hd.press(i1563.first, i1563.second, 1.0, Qt::NoModifier));
    refresh();
    state(u"new canal"_s);
    grab(u"panel_new.png"_s);
    for (const QString &mode : {u"start"_s, u"none"_s, u"decision"_s}) {
        cp.ed_open->setCurrentIndex(cp.ed_open->findData(mode));
        cp._open_changed();
        refresh();
        state(u"opens: "_s + mode);
    }
    cp.ed_name->setText(u"  Big   Ditch "_s);
    cp._name_edited();
    refresh();
    state(u"named"_s);
    cp._name_edited();
    cp._arm(u"prov"_s);
    const Pt i364 = *geo.inner(364);
    hd.press(i364.first, i364.second, 1.0, Qt::NoModifier);
    refresh();
    state(u"province picked"_s);
    Q_EMIT cp.ed_prov->pickRequested();
    const Pt i366 = *geo.inner(366);
    hd.press(i366.first, i366.second, 1.0, Qt::NoModifier);
    const Pt i3313 = *geo.inner(3313);
    cp._arm(u"prov"_s);
    hd.press(i3313.first, i3313.second, 1.0, Qt::NoModifier);
    state(u"sea tile as province"_s);
    cp._arm(u"a"_s);
    hd.press(i366.first, i366.second, 1.0, Qt::NoModifier);
    state(u"land as sea tile"_s);
    Q_EMIT cp.ed_b->pickRequested();
    hd.press(i3313.first, i3313.second, 1.0, Qt::NoModifier);
    refresh();
    state(u"sea tile b picked"_s);
    cp.ed_prov->spin->setValue(1563);
    cp.ed_a->spin->setValue(3266);
    cp._fields_edited();
    refresh();
    state(u"typed"_s);
    cp._model_picked(u"Kiel_Canal"_s);
    refresh();
    cp.nudge[u"down"_s]->click();
    refresh();
    state(u"own canal: model + nudge"_s);
    // --- wrong new canals, Esc
    cp.new_btn->setChecked(true);
    hd.press(i3313.first, i3313.second, 1.0, Qt::NoModifier);
    state(u"new on a sea tile"_s);
    cp.new_btn->setChecked(true);
    const Pt i106 = *geo.inner(106);
    hd.press(i106.first, i106.second, 1.0, Qt::NoModifier);
    state(u"new inland"_s);
    cp._arm(u"prov"_s);
    {
        const bool got = hd.press(-5.0, -5.0, 1.0, Qt::NoModifier);
        out << u"press off the map %1 pick %2"_s.arg(v(got), v(cp.pick));
        const bool c1 = hd.cancel();
        const bool c2 = hd.cancel();
        out << u"cancel %1 %2"_s.arg(v(c1), v(c2));
    }
    state(u"cancelled"_s);
    // --- describe, zoom, map clicks
    for (const std::optional<QVariantList> &tag : std::vector<std::optional<QVariantList>>{
             QVariantList{u"canal"_s, 1LL}, QVariantList{u"canal"_s, 33LL}, QVariantList{u"canal"_s, 3LL},
             QVariantList{u"canal"_s, 999LL}, QVariantList{u"row"_s, 1LL}, std::nullopt})
        out << u"describe %1 %2"_s.arg(tg(tag), v(cp.describe(tag)));
    cp.map_clicked(QVariantList{u"canal"_s, 2LL});
    cp._zoom();
    Q_EMIT cp.tree->itemDoubleClicked(cp.tree->topLevelItem(4), 0);
    state(u"canal 2 from the map, zoom"_s);
    cp.select(5, true);
    state(u"canal 5"_s);
    // --- delete
    cp.select(33);
    cp._delete();
    refresh();
    state(u"own canal deleted"_s);
    cp.select(3);
    cp._delete();
    refresh();
    state(u"canal 3 deleted"_s);
    cp._delete();
    // --- undo to the start
    for (int k = 0; k < 60; ++k) {
        const auto u = doc.undo();
        out << u"undo "_s + (u ? v(*u) : u"None"_s);
        if (!u)
            break;
    }
    refresh();
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
