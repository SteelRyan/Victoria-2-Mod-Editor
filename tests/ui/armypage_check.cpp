// The Armies page against Python, the C++ side of tools/check_armypage.py.
// Usage: med_armypage_check --run <mod copy> <backups> <out folder> <work folder>   (MODEDITOR_SETTINGS: the settings)
#include "ui/changelog.h"
#include "../core/nopopup.h"
#include "../core/pyform.h"
#include "app/mainwindow.h"
#include "core/countryhist.h"
#include "core/document.h"
#include "core/moddata.h"
#include "core/oob.h"
#include "core/pypath.h"
#include "map/mapview_data.h"
#include "ui/armypanel.h"
#include "ui/dialogs.h"
#include "ui/leaderbox.h"
#include "ui/mapview.h"
#include "ui/nationpanel.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGridLayout>
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

static QString key(const std::optional<long long> &k) { return k ? QString::number(*k) : u"None"_s; }
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
                       v(s.halo),
                       s.ring ? rgb(*s.ring) : u"None"_s,
                       v(s.warn),
                       v(s.hollow),
                       v(s.dash),
                       v(s.dy),
                       v(s.min_scale)}
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
    armypanel::ArmyPanel *ap = win.army_panel;
    const Qt::KeyboardModifiers nomod = Qt::NoModifier;
    oob::OOBEdits &oob = d.map<oob::OOBEdits>(u"oob"_s);

    auto oobs = [&] {
        QStringList ts;
        for (const auto &[t, _s] : oob.want)
            ts << t;
        std::sort(ts.begin(), ts.end());
        QStringList lines;
        for (const QString &t : ts) {
            const oob::OOB &st = *oob.want.get(t);
            for (const oob::Army &a : st.armies) {
                QStringList rs;
                for (const oob::Regiment &r : a.regiments)
                    rs << u"%1:%2:%3"_s.arg(r.name, r.type, key(r.home));
                lines << u"%1 A %2|%3|%4|%5"_s.arg(t, a.name, key(a.location), rs.join(u';'), a.extra.join(QChar(0x1f)));
            }
            for (const oob::Navy &n : st.navies) {
                QStringList ss;
                for (const oob::Ship &s : n.ships)
                    ss << u"%1:%2"_s.arg(s.name, s.type);
                lines << u"%1 N %2|%3|%4|%5"_s.arg(t, n.name, key(n.location), ss.join(u';'), n.extra.join(QChar(0x1f)));
            }
        }
        return u"%1 %2"_s.arg(lines.size()).arg(h(lines.join(u'\n').toUtf8()));
    };
    auto marks = [&] {
        const std::vector<mapview::Shape> sh = ap->shapes();
        QStringList all, desc;
        for (const mapview::Shape &s : sh) {
            all << shp(s);
            if (s.tag)
                desc << ap->describe(*s.tag);
        }
        return u"%1 %2 | desc %3"_s.arg(sh.size()).arg(h(all.join(u'\n').toUtf8()), h(desc.join(u'\n').toUtf8()));
    };
    auto common = [&](const QString &tag) {
        out << u"%1 | mode %2 | tab %3 %4 | nation %5 | cm %6 | msg %7 | %8 | steps %9 | oob %10 | marks %11"_s.arg(
            tag, win.mode, win.tab, v(win.stack->currentWidget() == ap), key(win.nation_tag), win.color_mode,
            v(win.st_msg->text()), win.st_pending->text(), QString::number(d.undo_stack.size()))
                   .arg(oobs(), marks());
    };
    auto combo = [](const QComboBox *cb) {
        return u"%1/%2 %3"_s.arg(cb->currentIndex()).arg(cb->count()).arg(v(cb->currentText()));
    };
    auto grid = [&](QGridLayout *g) {
        QStringList parts;
        for (int i = 0; i < g->count(); ++i) {
            QWidget *w = g->itemAt(i)->widget();
            if (auto *e = qobject_cast<QLineEdit *>(w))
                parts << u"E "_s + v(e->text());
            else if (auto *c = qobject_cast<QComboBox *>(w))
                parts << u"C "_s + combo(c);
            else if (auto *s = qobject_cast<widgets::Stepper *>(w))
                parts << u"S %1"_s.arg(s->value());
            else if (auto *b = qobject_cast<QPushButton *>(w))
                parts << u"B %1 %2"_s.arg(v(b->text()), v(b->toolTip()));
            else if (auto *el = qobject_cast<widgets::ElideLabel *>(w))
                parts << u"L %1 %2 %3"_s.arg(v(el->text()), v(el->toolTip()), v(el->styleSheet()));
            else if (auto *l = qobject_cast<QLabel *>(w))
                parts << u"L %1 %2 %3"_s.arg(v(l->text()), v(l->toolTip()), v(l->styleSheet()));
        }
        return parts.join(u';');
    };
    auto lst = [&](QListWidget *w) {
        QStringList rows;
        for (int i = 0; i < w->count(); ++i)
            rows << u"%1/%2"_s.arg(w->item(i)->text(), w->item(i)->toolTip());
        return u"%1 | cur %2 | h %3"_s.arg(!rows.isEmpty() ? h(rows.join(u'\n').toUtf8()) : u"-"_s)
            .arg(w->currentRow())
            .arg(w->height());
    };
    auto leader = [&](leaderbox::LeaderBox *lb) {
        return u"vis %1 | %2 | %3 | %4 [%5] | %6 [%7] | %8 | %9 | rm %10 | %11"_s.arg(
            v(lb->isVisibleTo(ap)), v(lb->head->text()), v(lb->name->text()), combo(lb->pers),
            QString::number(lb->pers->count()), combo(lb->back), QString::number(lb->back->count()),
            v(lb->prestige->text()), v(lb->set_btn->text()))
            .arg(v(lb->rm_btn->isEnabled()), v(lb->warn->text()));
    };
    auto vis = [&](QWidget *w) { return v(w->isVisibleTo(ap)); };
    auto asnap = [&](const QString &tag) {
        common(tag);
        QStringList segs;
        for (const auto &[k, b] : ap->seg)
            if (b->isChecked())
                segs << k;
        out << u"%1 P %2 | %3 | %4 | %5 | rule %6 %7 | sub %8 %9 | en %10 %11"_s.arg(
            tag, combo(ap->picker), v(ap->title->text()), v(ap->summary->text()), v(ap->file_note->text()),
            v(ap->rule_note->text()), vis(ap->rule_note), ap->sub, segs.join(u','))
                   .arg(v(ap->armies_box->isEnabled()), vis(ap->armies_box));
        out << u"%1 A %2 | del %3 | sec %4 %5 %6 | %7 %8 | name %9 | loc %10 | near %11 | hint %12"_s.arg(
            tag, lst(ap->army_list), v(ap->del_army_btn->isEnabled()), vis(ap->army_section), vis(ap->comp_section),
            vis(ap->name_section), v(ap->army_title->text()), v(ap->army_title->styleSheet()), v(ap->army_name->text()))
                   .arg(v(ap->army_loc->text()), v(ap->near_btn->isEnabled()), vis(ap->army_hint));
        out << u"%1 B %2"_s.arg(tag, grid(ap->brig_grid));
        out << u"%1 T %2 | %3 | %4 | add %5 | sort %6 | scheme %7 | pat %8 %9"_s.arg(
            tag, v(ap->comp_title->text()), grid(ap->comp_grid), v(ap->comp_total->text()), v(ap->comp_add->isEnabled()),
            v(ap->sort_types->isChecked()), combo(ap->scheme), vis(ap->pattern), v(ap->pattern->text()));
        out << u"%1 GL %2"_s.arg(tag, leader(ap->army_leader));
        out << u"%1 N %2 | %3 | del %4 | sec %5 %6 | %7 | name %8 | loc %9 | add %10 %11 | scheme %12 | pat %13"_s.arg(
            tag, vis(ap->navies_box), lst(ap->navy_list), v(ap->del_navy_btn->isEnabled()), vis(ap->navy_section),
            vis(ap->ship_name_section), v(ap->navy_title->text()), v(ap->navy_name->text()), v(ap->navy_loc->text()))
                   .arg(combo(ap->add_type), QString::number(ap->add_count->value()), combo(ap->ship_scheme),
                        vis(ap->ship_pattern));
        out << u"%1 S %2"_s.arg(tag, grid(ap->ship_grid));
        out << u"%1 AL %2"_s.arg(tag, leader(ap->navy_leader));
    };
    auto pick = [](QComboBox *cb, const QString &data) {
        cb->setCurrentIndex(cb->findData(data));
        Q_EMIT cb->activated(cb->currentIndex());
    };
    auto choose = [](QComboBox *cb) {
        for (int i = 0; i < cb->count(); ++i)
            if (i != cb->currentIndex()) {
                cb->setCurrentIndex(i);
                break;
            }
        Q_EMIT cb->activated(cb->currentIndex());
    };
    auto widgets_of = [](QGridLayout *g, auto *proto) {
        using T = std::remove_pointer_t<decltype(proto)>;
        QList<T *> r;
        for (int i = 0; i < g->count(); ++i)
            if (auto *w = qobject_cast<T *>(g->itemAt(i)->widget()))
                r << w;
        return r;
    };
    auto table = [&](const QString &k) {
        const QString was = win.color_mode;
        win.color_mode = k;
        const mapview::ColorTable t = win.color_table();
        win.color_mode = was;
        out << u"table %1 [%2, 3] %3"_s.arg(k).arg(md->n).arg(h(t.rgb.data(), t.rgb.size()));
    };
    auto click_mark = [&](const std::function<bool(const mapview::Shape &)> &want) {
        for (const mapview::Shape &s : ap->shapes())
            if (want(s)) {
                const QPointF p = s.pts[0];
                out << u"hook "_s + v(win._map_click_hook(p.x(), p.y(), 0.5, nomod));
                QApplication::processEvents();
                return;
            }
        throw std::runtime_error("no such mark");
    };

    countryhist::CountryHistory &hist = *m->country_hist;
    QStringList tags;
    for (const auto &[t, _c] : m->countries)
        tags << t;
    std::sort(tags.begin(), tags.end());
    std::map<QString, std::vector<long long>> owned;
    for (const QString &t : tags)
        owned[t] = nationpanel::stats_owned(d, *m, t);
    QStringList cands;
    for (const QString &t : tags)
        if (hist.files.contains(t) && owned[t].size() >= 3 && oob.state(t).armies.size() >= 2 &&
            oob.state(t).navies.size() >= 1)
            cands << t;
    const QString tag = cands[0], tag2 = cands[1];
    out << u"nation %1 other %2"_s.arg(tag, tag2);
    long long sea = -1;
    for (const auto &[p, pr] : m->provinces)
        if (pr.sea) {
            sea = p;
            break;
        }
    win.set_tab(u"armies"_s);
    QApplication::processEvents();
    asnap(u"open"_s);
    pick(ap->picker, tag);
    QApplication::processEvents();
    asnap(u"picked"_s);
    ap->grab().save(QDir(out_dir).filePath(u"side_armies.png"_s));
    for (const QString &k : {u"military"_s, u"soldiers"_s})
        table(k);
    // --- hover: a shield, a number
    for (const QString &kind : {u"army"_s, u"badge"_s}) {
        for (const mapview::Shape &s : ap->shapes())
            if (s.kind == kind) {
                const long long i = (*s.tag)[1].toLongLong();
                const long long pid = kind == u"badge" ? i : *ap->st()->armies[size_t(i)].location;
                win.on_hover(pid, int(s.pts[0].x()), int(s.pts[0].y()));
                out << u"hover %1 %2"_s.arg(kind, v(win.st_hover->text()));
                break;
            }
    }
    // --- an army: a brigade added on the map, one taken away, the nearest one
    ap->army_list->setCurrentRow(0);
    QApplication::processEvents();
    asnap(u"army 0"_s);
    win.map->grab().save(QDir(out_dir).filePath(u"map_army.png"_s));
    click_mark([](const mapview::Shape &s) { return s.kind == u"badge" && s.hollow; });
    asnap(u"attached"_s);
    {
        std::vector<long long> homes;
        for (const oob::Regiment &r : ap->army().second->regiments)
            if (r.home)
                homes.push_back(*r.home);
        win.on_right_click(homes.back(), nomod);
    }
    asnap(u"detached"_s);
    win.on_right_click(sea, nomod);
    ap->near_btn->click();
    asnap(u"nearest"_s);
    // --- unit types
    {
        widgets::Stepper *st = ap->comp_rows[0].st;
        st->setValue(st->value() + 2);
    }
    asnap(u"comp more"_s);
    ap->comp_add->click();
    asnap(u"comp add"_s);
    choose(ap->comp_rows.back().cb);
    asnap(u"comp type"_s);
    ap->comp_rows.back().x->click();
    asnap(u"comp drop"_s);
    ap->comp_btn->click();
    QApplication::processEvents();
    asnap(u"set types"_s);
    // --- names
    pick(ap->scheme, u"custom"_s);
    ap->pattern->setText(u"{n}. {type} of {army}"_s);
    ap->rename_btn->click();
    asnap(u"renamed"_s);
    pick(ap->scheme, u"nation"_s);
    ap->rename_btn->click();
    asnap(u"renamed nation"_s);
    {
        QLineEdit *e = widgets_of(ap->brig_grid, static_cast<QLineEdit *>(nullptr))[0];
        e->setText(u"  The \"Old\" Guard "_s);
        Q_EMIT e->editingFinished();
    }
    asnap(u"brigade renamed"_s);
    choose(widgets_of(ap->brig_grid, static_cast<QComboBox *>(nullptr))[0]);
    asnap(u"brigade type"_s);
    widgets_of(ap->brig_grid, static_cast<QPushButton *>(nullptr)).last()->click();
    asnap(u"brigade removed"_s);
    ap->army_name->setText(u"Test Army"_s);
    Q_EMIT ap->army_name->editingFinished();
    asnap(u"army renamed"_s);
    // --- moved on the map
    ap->army_move->click();
    common(u"move"_s);
    win.on_click(sea, nomod);
    asnap(u"move sea"_s);
    ap->army_move->click();
    win.on_click(owned[tag2][0], nomod);
    asnap(u"moved"_s);
    ap->army_move->click();
    win.escape();
    asnap(u"move esc"_s);
    // --- the general
    {
        leaderbox::LeaderBox *lb = ap->army_leader;
        lb->name->setText(u""_s);
        lb->set_btn->click();
        asnap(u"general no name"_s);
        lb->name->setText(u" General  \"Test\"  Smith "_s);
        lb->prestige->setText(u"abc"_s);
        lb->set_btn->click();
        asnap(u"general bad prestige"_s);
        lb->prestige->setText(u"0.2"_s);
        if (lb->pers->count() > 1)
            lb->pers->setCurrentIndex(1);
        lb->set_btn->click();
        asnap(u"general set"_s);
        lb->rm_btn->click();
        asnap(u"general removed"_s);
    }
    // --- a new army, deleted
    ap->new_army_btn->click();
    asnap(u"new army"_s);
    ap->del_army_btn->click();
    asnap(u"army deleted"_s);
    ap->army_list->setCurrentRow(1);
    win.escape();
    asnap(u"esc"_s);
    click_mark([](const mapview::Shape &s) { return s.kind == u"army"; });
    asnap(u"shield"_s);
    click_mark([](const mapview::Shape &s) { return s.kind == u"army" && s.halo; });
    asnap(u"shield again"_s);
    click_mark([](const mapview::Shape &s) { return s.kind == u"badge" && !s.hollow; });
    asnap(u"number, no army"_s);
    win.on_click(owned[tag2][0], nomod);
    QApplication::processEvents();
    asnap(u"other nation"_s);
    pick(ap->picker, tag);
    QApplication::processEvents();
    // --- navies
    win.go_page(u"navies"_s);
    QApplication::processEvents();
    asnap(u"navies"_s);
    ap->grab().save(QDir(out_dir).filePath(u"side_navies.png"_s));
    ap->navy_list->setCurrentRow(0);
    QApplication::processEvents();
    asnap(u"navy 0"_s);
    if (ap->add_type->count() > 1)
        ap->add_type->setCurrentIndex(1);
    ap->add_count->setValue(3);
    ap->add_ship_btn->click();
    asnap(u"ships added"_s);
    pick(ap->ship_scheme, u"custom"_s);
    ap->ship_pattern->setText(u"HMS {type} {n} ({navy})"_s);
    ap->add_ship_btn->click();
    asnap(u"ships custom"_s);
    pick(ap->ship_scheme, u"type"_s);
    ap->ship_rename_btn->click();
    asnap(u"ships renamed"_s);
    {
        QLineEdit *e = widgets_of(ap->ship_grid, static_cast<QLineEdit *>(nullptr))[0];
        e->setText(u"Victory"_s);
        Q_EMIT e->editingFinished();
    }
    asnap(u"ship renamed"_s);
    choose(widgets_of(ap->ship_grid, static_cast<QComboBox *>(nullptr))[0]);
    asnap(u"ship type"_s);
    widgets_of(ap->ship_grid, static_cast<QPushButton *>(nullptr)).last()->click();
    asnap(u"ship removed"_s);
    ap->navy_name->setText(u"Home Fleet"_s);
    Q_EMIT ap->navy_name->editingFinished();
    asnap(u"navy renamed"_s);
    ap->navy_move->click();
    win.on_click(sea, nomod);
    asnap(u"navy moved"_s);
    ap->navy_leader->name->setText(u"Nelson"_s);
    ap->navy_leader->set_btn->click();
    asnap(u"admiral set"_s);
    ap->new_navy_btn->click();
    asnap(u"new navy"_s);
    ap->del_navy_btn->click();
    asnap(u"navy deleted"_s);
    ap->navy_list->setCurrentRow(0);
    click_mark([](const mapview::Shape &s) { return s.kind == u"anchor" && s.halo; });
    asnap(u"anchor again"_s);
    win.go_page(u"armies"_s);
    QApplication::processEvents();
    asnap(u"armies again"_s);
    // --- the main window's undo
    out << u"undo "_s + v(win.undo());
    QApplication::processEvents();
    asnap(u"undone"_s);
    // --- G4
    {
        const bool ok = win.save(false);
        out << u"apply %1 | msg %2 | %3"_s.arg(v(ok), v(win.st_msg->text()), win.st_pending->text());
    }
    QApplication::processEvents();
    asnap(u"applied"_s);
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
