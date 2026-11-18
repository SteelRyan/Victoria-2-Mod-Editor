// The Diplomacy page against Python, the C++ side of tools/check_diplopage.py.
// Usage: med_diplopage_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/countryhist.h"
#include "core/diplomacy.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/oob.h"
#include "core/pypath.h"
#include "core/pytext.h"
#include "map/mapview_data.h"
#include "ui/dialogs.h"
#include "ui/diplopanel.h"
#include "ui/mapview.h"
#include "ui/modepanel.h"
#include "ui/nationpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>

using namespace Qt::StringLiterals;
using pyform::v;

static QString h(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16));
}
static QString h(const void *p, size_t n)
{
    return h(QByteArray(static_cast<const char *>(p), qsizetype(n)));
}

static constexpr int MAP_W = 1000, MAP_H = 700, SIDE_W = 470;
static const QStringList MODES = {u"status"_s, u"relvalues"_s, u"spheres"_s, u"subjects"_s, u"alliances"_s};

static QString key(const std::optional<QString> &k) { return k.value_or(u"None"_s); }
static QString rgb(const std::tuple<int, int, int> &c)
{
    return u"%1,%2,%3"_s.arg(std::get<0>(c)).arg(std::get<1>(c)).arg(std::get<2>(c));
}

static QString shp(const mapview::Shape &s)
{
    QStringList pts;
    for (const QPointF &p : s.pts)
        pts << u"%1,%2"_s.arg(v(p.x()), v(p.y()));
    QStringList tag;
    if (s.tag)
        for (const QVariant &x : *s.tag)
            tag << x.toString();
    return QStringList{s.kind,
                       pts.join(u';'),
                       rgb(s.color),
                       s.width ? v(*s.width) : u"None"_s,
                       s.size ? v(*s.size) : u"None"_s,
                       s.text,
                       s.tag ? tag.join(u':') : u"None"_s,
                       v(s.halo)}
        .join(u'|');
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
    win.resize(1500, 900);
    win.show();
    win.side_scroll->setFixedWidth(SIDE_W);
    win.map->setFixedSize(MAP_W, MAP_H);
    QApplication::processEvents();
    win._check_timer->stop();
    win.map->fit();
    QApplication::processEvents();
    document::Document &d = *win.doc;
    diplopanel::DiploPanel *dp = win.diplo_panel;
    const Qt::KeyboardModifiers nomod = Qt::NoModifier;
    oob::OOBEdits &oob = d.map<oob::OOBEdits>(u"oob"_s);
    diplomacy::DiploEdits &dip = d.map<diplomacy::DiploEdits>(u"diplomacy"_s);

    auto state = [&] {
        QStringList ts;
        for (const auto &[t, _s] : oob.want)
            ts << t;
        std::sort(ts.begin(), ts.end());
        QStringList lines;
        for (const QString &t : ts)
            for (const auto &[b, r] : oob.want.get(t)->relations)
                lines << u"%1 R %2 %3 %4 %5"_s.arg(t, b, key(r.value), key(r.level), key(r.influence_value));
        for (const diplomacy::Record &r : dip.records)
            lines << u"T %1 %2 %3 %4 %5 %6"_s.arg(r.kind, r.first, r.second, key(r.start), key(r.end), r.file);
        return u"%1 %2"_s.arg(lines.size()).arg(h(lines.join(u'\n').toUtf8()));
    };
    auto marks = [&] {
        const std::vector<mapview::Shape> sh = dp->shapes();
        QStringList all, desc;
        for (const mapview::Shape &s : sh) {
            all << shp(s);
            if (s.tag)
                desc << dp->describe(*s.tag);
        }
        return u"%1 %2 | desc %3"_s.arg(sh.size()).arg(h(all.join(u'\n').toUtf8()), h(desc.join(u'\n').toUtf8()));
    };
    auto common = [&](const QString &tag) {
        out << u"%1 | mode %2 | tab %3 %4 | nation %5 | b %6 | cm %7 | msg %8 | %9 | steps %10 | st %11 | marks %12"_s
                   .arg(tag, win.mode, win.tab, v(win.stack->currentWidget() == dp), key(win.nation_tag),
                        key(win.diplo_b), win.color_mode, v(win.st_msg->text()), win.st_pending->text())
                   .arg(QString::number(d.undo_stack.size()), state(), marks());
    };
    auto combo = [](const QComboBox *cb) {
        return u"%1/%2 %3"_s.arg(cb->currentIndex()).arg(cb->count()).arg(v(cb->currentText()));
    };
    auto lst = [&](QListWidget *w) {
        QStringList rows, sel;
        for (int i = 0; i < w->count(); ++i) {
            rows << u"%1/%2"_s.arg(w->item(i)->text(), w->item(i)->toolTip());
            if (w->item(i)->isSelected())
                sel << QString::number(i);
        }
        return u"%1 | cur %2 | sel %3 | h %4"_s.arg(!rows.isEmpty() ? h(rows.join(u'\n').toUtf8()) : u"-"_s)
            .arg(w->currentRow())
            .arg(sel.join(u','))
            .arg(w->height());
    };
    auto vis = [&](QWidget *w) { return v(w->isVisibleTo(dp)); };
    auto dsnap = [&](const QString &tag) {
        common(tag);
        QStringList segs;
        for (const auto &[k, b] : dp->seg)
            if (b->isChecked())
                segs << k;
        out << u"%1 M %2 | %3 | %4 | hint %5 | gp %6 %7 | clear %8 | seg %9 | shown %10 | view %11"_s.arg(
            tag, combo(dp->picker), v(dp->title->text()), v(dp->sub->text()), vis(dp->hint), v(dp->gp_label->text()),
            vis(dp->gp_label), vis(dp->clear_btn), segs.join(u','))
                   .arg(dp->shown, dp->view);
        out << u"%1 P %2 | %3 | rel %4 %5 | %6 | boxes %7 %8"_s.arg(
            tag, vis(dp->pair_section), v(dp->pair_title->text()), v(dp->rel->value()), v(dp->rel->edit->text()),
            v(dp->rel_note->text()), vis(dp->rel_box), vis(dp->st_box));
        out << u"%1 L %2 %3 | level %4 %5 | infl %6 %7 | %8"_s.arg(
            tag, v(dp->lvl_title->text()), vis(dp->lvl_title), QString::number(dp->level->value()), vis(dp->level),
            v(dp->infl->value()), vis(dp->infl_row), v(dp->lvl_note->text()));
        QStringList items;
        for (int i = 0; i < dp->subject->count(); ++i)
            items << dp->subject->itemText(i);
        out << u"%1 T %2 [%3] | ally %4 | %5 %6 | %7"_s.arg(tag, combo(dp->subject), items.join(u';'),
                                                           v(dp->alliance->isChecked()), v(dp->other_treaties->text()),
                                                           vis(dp->other_treaties), v(dp->treaty_note->text()));
        out << u"%1 R %2 | %3 | %4"_s.arg(tag, vis(dp->list_section), v(dp->list_title->text()), lst(dp->rel_list));
        out << u"%1 W %2 | %3 %4 | legends %5 %6"_s.arg(tag, v(dp->war_note->text()), lst(dp->war_list),
                                                       vis(dp->war_list), vis(dp->legend_status), vis(dp->legend_rel));
    };
    auto pick = [](QComboBox *cb, const QString &data) {
        cb->setCurrentIndex(cb->findData(data));
        Q_EMIT cb->activated(cb->currentIndex());
    };
    auto table = [&](const QString &k) {
        const QString was = win.color_mode;
        win.color_mode = k;
        const mapview::ColorTable t = win.color_table();
        win.color_mode = was;
        out << u"table %1 [%2%3, 3] %4"_s.arg(k).arg(md->n).arg(t.Q ? u", %1"_s.arg(t.Q) : u""_s)
                   .arg(h(t.rgb.data(), t.rgb.size()));
    };
    auto click_mark = [&](const std::function<bool(const mapview::Shape &)> &want) {
        for (const mapview::Shape &s : dp->shapes())
            if (want(s)) {
                const QPointF p = s.pts[0];
                out << u"hook "_s + v(win._map_click_hook(p.x(), p.y(), 0.5, nomod));
                QApplication::processEvents();
                return;
            }
        out << u"hook no mark"_s;
    };
    auto slide = [&](widgets::SliderField *f, int val) {
        f->setValue(val);
        Q_EMIT f->valueChanged(val);
        dp->_timer->stop();
        dp->_flush();
        QApplication::processEvents();
    };

    countryhist::CountryHistory &hist = *m->country_hist;
    countryhist::NationEdits &nat = d.map<countryhist::NationEdits>(u"nations"_s);
    QStringList tags;
    for (const auto &[t, _c] : m->countries)
        tags << t;
    std::sort(tags.begin(), tags.end());
    std::map<QString, std::vector<long long>> owned;
    for (const QString &t : tags)
        owned[t] = nationpanel::stats_owned(d, *m, t);
    const QStringList gps = diplomacy::great_powers(*m, nat);
    auto has_cap = [&](const QString &t) {
        const std::optional<QString> cap = hist.files.contains(t) ? nat.state(t).at(u"capital"_s) : std::nullopt;
        if (!cap || cap->isEmpty() || !py::isdigit(*cap))
            return false;
        const long long c = py::to_int(*cap);
        return 0 < c && c < md->n && bool(md->area[size_t(c)]);
    };
    QStringList sg = gps;
    std::sort(sg.begin(), sg.end());
    const QString ta = sg[0], tb2 = sg[1];
    QStringList others;
    for (const QString &t : tags)
        if (!gps.contains(t) && !owned[t].empty() && has_cap(t) && hist.files.contains(t))
            others << t;
    const QString tb = others[0], tc = others[1];
    out << u"gps %1 | a %2 | b %3 | gp2 %4 | c %5"_s.arg(gps.join(u','), ta, tb, tb2, tc);
    long long sea = -1;
    for (const auto &[p, pr] : m->provinces)
        if (pr.sea) {
            sea = p;
            break;
        }
    win.set_tab(u"diplomacy"_s);
    QApplication::processEvents();
    dsnap(u"open"_s);
    for (const QString &k : MODES)
        table(k);
    win.on_click(owned[ta][0], nomod);
    QApplication::processEvents();
    dsnap(u"clicked a"_s);
    dp->grab().save(QDir(out_dir).filePath(u"side_relations.png"_s));
    for (const QString &k : MODES)
        table(k);
    for (const mapview::Shape &s : dp->shapes())
        if (s.kind == u"badge") {
            win.on_hover(owned[ta][0], int(s.pts[0].x()), int(s.pts[0].y()));
            out << u"hover "_s + v(win.st_hover->text());
            break;
        }
    click_mark([](const mapview::Shape &s) { return s.kind == u"badge"; });
    dsnap(u"badge"_s);
    win.on_click(owned[tb][0], nomod);
    QApplication::processEvents();
    dsnap(u"b"_s);
    win.map->grab().save(QDir(out_dir).filePath(u"map_pair.png"_s));
    for (int val : {57, 0, -120}) {
        slide(dp->rel, val);
        dsnap(u"rel %1"_s.arg(val));
    }
    table(u"relvalues"_s);
    for (const auto &[k, b] : dp->seg)
        if (k == u"status")
            b->click();
    QApplication::processEvents();
    dsnap(u"status view"_s);
    dp->grab().save(QDir(out_dir).filePath(u"side_status.png"_s));
    dp->level->setValue(5);
    Q_EMIT dp->level->valueChanged(5);
    QApplication::processEvents();
    dsnap(u"level 5"_s);
    slide(dp->infl, 40);
    dsnap(u"infl 40"_s);
    for (int i : {1, 4, 0}) {
        dp->subject->setCurrentIndex(i);
        Q_EMIT dp->subject->activated(i);
        QApplication::processEvents();
        dsnap(u"subject %1"_s.arg(i));
    }
    dp->alliance->click();
    QApplication::processEvents();
    dsnap(u"alliance on"_s);
    for (const QString &k : MODES)
        table(k);
    dp->alliance->click();
    QApplication::processEvents();
    dsnap(u"alliance off"_s);
    dp->level->setValue(2);
    Q_EMIT dp->level->valueChanged(2);
    QApplication::processEvents();
    dsnap(u"level 2"_s);
    win.on_click(owned[tb2][0], nomod);
    QApplication::processEvents();
    dsnap(u"gp pair"_s);
    dp->swap_btn->click();
    QApplication::processEvents();
    dsnap(u"swapped"_s);
    win.on_right_click(owned[tc][0], nomod);
    QApplication::processEvents();
    dsnap(u"right-click"_s);
    win.on_right_click(sea, nomod);
    win.on_click(owned[ta][0], Qt::ShiftModifier);
    QApplication::processEvents();
    dsnap(u"shift"_s);
    win.on_click(sea, nomod);
    dsnap(u"sea"_s);
    if (dp->rel_list->count()) {
        Q_EMIT dp->rel_list->itemClicked(dp->rel_list->item(0));
        QApplication::processEvents();
    }
    dsnap(u"list row"_s);
    for (const auto &[k, b] : dp->seg)
        if (k == u"relations")
            b->click();
    QApplication::processEvents();
    dsnap(u"relations view"_s);
    win.color_box->setCurrentIndex(win.color_box->findData(u"status"_s));
    QApplication::processEvents();
    dsnap(u"mode status"_s);
    win.color_box->setCurrentIndex(win.color_box->findData(u"relvalues"_s));
    QApplication::processEvents();
    dsnap(u"mode relvalues"_s);
    win.escape();
    QApplication::processEvents();
    dsnap(u"esc b"_s);
    win.escape();
    QApplication::processEvents();
    dsnap(u"esc a"_s);
    pick(dp->picker, tb2);
    QApplication::processEvents();
    dsnap(u"picker"_s);
    dp->clear_btn->click();
    QApplication::processEvents();
    dsnap(u"cleared"_s);
    // --- the main window's undo
    out << u"undo "_s + v(win.undo());
    QApplication::processEvents();
    dsnap(u"undone"_s);
    // --- G4
    {
        const bool ok = win.save(false);
        out << u"apply %1 | msg %2 | %3"_s.arg(v(ok), v(win.st_msg->text()), win.st_pending->text());
    }
    QApplication::processEvents();
    dsnap(u"applied"_s);
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
