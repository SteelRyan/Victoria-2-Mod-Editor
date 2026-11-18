// Armies → Generals / Navies → Admirals and Warfare → Leader traits (C++ only, 2026-11-17): the lists by scope, an edit,
// a new / copied / deleted leader, the traits' mass edit (add, copies, delete with the leaders who had one), a new
// trait, then Apply — and what the files say afterwards. Pictures of the pages into <shots>.
// Usage: med_leaders_check <mod folder (a scratch copy)> <backups> <shots>   (offscreen, MODEDITOR_SETTINGS of its own,
// MODEDITOR_KEEP_COPY=0)
#include "../core/nopopup.h"
#include "app/mainwindow.h"
#include "core/moddata.h"
#include "core/oob.h"
#include "core/pypath.h"
#include "core/script.h"
#include "core/scriptstore.h"
#include "core/traits.h"
#include "ui/armypanel.h"
#include "ui/entryview.h"
#include "ui/traitnames.h"
#include "ui/iconboard.h"
#include "ui/buildtypes.h"
#include "ui/iconstrips.h"
#include "map/iconstrips_data.h"
#include "ui/nationpanel.h"
#include "ui/reformwin.h"
#include "core/countryhist.h"
#include "core/localisation.h"
#include <QTreeWidget>
#include <QCheckBox>
#include <QKeyEvent>
#include "ui/widgets.h"
#include <QLineEdit>
#include <map>
#include "ui/leaderbox.h"
#include "ui/leaderlist.h"
#include "ui/mapview.h"
#include "ui/theme.h"
#include "ui/traitodds.h"
#include "ui/unittimeline.h"
#include "ui/unitpanel.h"
#include "core/techdata.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QFile>
#include <QToolButton>
#include "ui/moreviews.h"
#include <QScrollArea>
#include <QRegularExpression>
#include <QDir>
#include <QDirIterator>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QCheckBox>
#include "ui/widgets.h"
#include <cstdio>

using namespace Qt::StringLiterals;

static int fails = 0;
static void ok(bool c, const QString &what)
{
    std::printf("%s %s\n", c ? "ok  " : "FAIL", qUtf8Printable(what));
    std::fflush(stdout);
    if (!c)
        ++fails;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    theme::apply(app);
    if (argc < 4)
        return 2;
    const QString root = QString::fromLocal8Bit(argv[1]), backups = QString::fromLocal8Bit(argv[2]),
                  shots = QString::fromLocal8Bit(argv[3]);
    QDir().mkpath(shots);
    auto m = std::make_shared<moddata::Mod>(root, pypath::dirname(pypath::dirname(root)), std::nullopt, QStringList{});
    m->load();
    auto md = std::make_shared<mapview::MapData>(*m, m->p({u"map"_s, m->provinces_bmp}));
    mainwindow::MainWindow win(m, md, m->scenarios()[0].name, backups);
    win._check_timer->stop();
    win._rec_timer->stop();
    win.resize(1600, 1000);
    win.show();
    QApplication::processEvents();
    auto shot = [&](const QString &name) {
        QApplication::processEvents();
        win.grab().save(pypath::join(shots, name));
    };
    leaderlist::LeaderList *L = win.army_panel->leaders;
    auto &ob = win.doc->map<oob::OOBEdits>(u"oob"_s);

    // --- Generals of one nation
    win.go_page(u"generals"_s);
    win.show_nation(u"USA"_s);
    QApplication::processEvents();
    ok(win.army_panel->sub == u"generals" && L->isVisible(), u"Armies → Generals is open"_s);
    const size_t usa = L->rows.size();
    ok(usa > 0, u"USA's generals listed: %1"_s.arg(usa));
    long long pool = 0;
    for (const auto &r : L->rows)
        pool += r.where == 0;
    ok(pool > 0, u"…of them in the pool: %1"_s.arg(pool));
    shot(u"1 generals USA.png"_s);
    // --- the world, and nations picked on the map
    L->scope_btns[1].second->click();   // (World)
    QApplication::processEvents();
    const size_t world = L->rows.size();
    ok(world > usa * 5, u"the world: %1 generals"_s.arg(world));
    L->pick_only(u"FRA"_s);                      // (a click on France, then Ctrl+click on Prussia)
    L->toggle_pick(u"PRU"_s);
    QApplication::processEvents();
    ok(L->scope == u"picked" && L->tags_shown() == QStringList({u"FRA"_s, u"PRU"_s}),
       u"Ctrl+click picks: %1 → %2 generals"_s.arg(L->tags_shown().join(u", "_s)).arg(L->rows.size()));
    shot(u"2 generals FRA PRU.png"_s);
    // --- the statistics of the leaders listed: how many have each attack, the best by attack + defence
    {
        std::map<double, int> by_atk;
        double top = -1e9;
        for (size_t i : L->shown_rows) {
            ++by_atk[L->rows[i].atk];
            top = std::max(top, L->rows[i].atk + L->rows[i].def);
        }
        ok(L->shown_rows.size() == L->rows.size() && !by_atk.empty(), u"statistics over %1 generals"_s.arg(L->shown_rows.size()));
        ok(L->best->topLevelItemCount() >= int(std::min<size_t>(5, L->shown_rows.size())) &&
               L->best->topLevelItem(0)->text(3).toDouble() == top,
           u"the best by attack + defence: %1 (%2)"_s.arg(L->best->topLevelItem(0)->text(1), L->best->topLevelItem(0)->text(3)));
        const auto [v, n] = *by_atk.rbegin();
        L->filter->setText(u"attack = %1"_s.arg(v));
        QApplication::processEvents();
        ok(int(L->shown_rows.size()) == n, u"filter \"attack = %1\" lists %2 (want %3)"_s.arg(v).arg(L->shown_rows.size()).arg(n));
        L->filter->clear();
        QApplication::processEvents();
        L->grab().save(pypath::join(shots, u"2b statistics FRA PRU.png"_s));
    }
    // --- an edit in the pool
    L->scope_btns[0].second->click();
    L->pick_only(u"USA"_s);
    win.show_nation(u"USA"_s);
    QApplication::processEvents();
    long long first_pool = -1;
    QString old_name;
    for (const auto &r : L->rows)
        if (r.where == 0) {
            first_pool = r.index;
            old_name = r.f.value(u"name"_s);
            break;
        }
    L->select(u"USA"_s, 0, first_pool);
    QApplication::processEvents();
    L->commit(u"name"_s, u"Check General"_s);
    QApplication::processEvents();
    ok(ob.state(u"USA"_s).leaders[size_t(first_pool)].get(u"name"_s) == u"Check General",
       u"renamed %1 → Check General (one undo step)"_s.arg(old_name));
    L->commit(u"personality"_s, u"relentless"_s);
    ok(ob.state(u"USA"_s).leaders[size_t(first_pool)].get(u"personality"_s) == u"relentless", u"personality → relentless"_s);
    L->commit(u"prestige"_s, u"abc"_s);
    ok(!L->warn->text().isEmpty() && ob.state(u"USA"_s).leaders[size_t(first_pool)].get(u"prestige"_s) != u"abc",
       u"a prestige that is no number is refused: "_s + L->warn->text());
    const size_t n0 = ob.state(u"USA"_s).leaders.size();
    L->new_leader();
    QApplication::processEvents();
    ok(ob.state(u"USA"_s).leaders.size() == n0 + 1, u"＋ New general: the pool has %1"_s.arg(n0 + 1));
    L->copy_leader();
    QApplication::processEvents();
    ok(ob.state(u"USA"_s).leaders.size() == n0 + 2, u"⧉ Copy: %1"_s.arg(n0 + 2));
    L->delete_leader();
    QApplication::processEvents();
    ok(ob.state(u"USA"_s).leaders.size() == n0 + 1, u"✕ Delete: %1"_s.arg(n0 + 1));
    // --- make several at once: 10 generals with attack 1 and defence 1, named from USA's culture; then Ctrl+Z
    {
        L->gen_open->click();
        QApplication::processEvents();
        L->gen_n->setValue(10);
        L->gen_only->setChecked(true);
        L->gen_atk->setValue(1);
        L->gen_def->setValue(1);
        QApplication::processEvents();
        ok(L->gen_go->isEnabled() && L->gen_go->text().contains(u"10 generals for USA"_s),
           u"the button: %1 / %2"_s.arg(L->gen_go->text(), L->gen_note->text()));
        L->gen_box->grab().save(pypath::join(shots, u"2c make several.png"_s));
        L->gen_go->click();
        QApplication::processEvents();
        const auto &ls = ob.state(u"USA"_s).leaders;
        ok(ls.size() == n0 + 11, u"10 made: the pool has %1"_s.arg(ls.size()));
        bool all11 = true, named = true;
        QStringList names;
        for (size_t i = n0 + 1; i < ls.size(); ++i) {
            double a = 0, d = 0;
            for (const auto &r : L->rows)
                if (r.tag == u"USA" && r.where == 0 && r.index == static_cast<long long>(i)) {
                    a = r.atk;
                    d = r.def;
                }
            all11 = all11 && a == 1 && d == 1;
            named = named && !ls[i].get(u"name"_s).startsWith(u"General "_s) && ls[i].get(u"name"_s).contains(u' ');
            names << ls[i].get(u"name"_s);
        }
        ok(all11, u"each has attack 1, defence 1"_s);
        ok(named && QSet<QString>(names.begin(), names.end()).size() == 10, u"names from the culture: %1"_s.arg(names.mid(0, 4).join(u", "_s)));
        L->grab().save(pypath::join(shots, u"2d after making.png"_s));
        win.doc->undo();
        QApplication::processEvents();
        ok(ob.state(u"USA"_s).leaders.size() == n0 + 1, u"Ctrl+Z takes the 10 back in one step"_s);
        L->gen_open->click();
    }
    // an army's general (CSA leads armies in 1861 only; the 1836 files keep them in the pool) — when there is one
    for (const auto &r : L->rows)
        if (r.where == 1) {
            L->select(r.tag, 1, r.index);
            const QString t = r.tag;
            const long long ai = r.index;
            L->commit(u"prestige"_s, u"0.3"_s);
            ok(leaderbox::leader_of(ob.state(t).armies[size_t(ai)].extra).second.value(u"prestige"_s) == u"0.3",
               u"the general leading %1 (%2): prestige 0.3"_s.arg(r.unit, t));
            break;
        }
    shot(u"3 generals edited.png"_s);
    // admirals
    win.go_page(u"admirals"_s);
    win.show_nation(u"ENG"_s);
    QApplication::processEvents();
    ok(L->kind == u"sea", u"Navies → Admirals: %1 admirals of ENG"_s.arg(L->rows.size()));
    shot(u"4 admirals ENG.png"_s);

    // --- Warfare → Leader traits
    win.go_page(u"warfare"_s);
    win.war_ws->set_page(u"traits"_s);
    QApplication::processEvents();
    auto *page = qobject_cast<traitodds::TraitsPage *>(win.war_ws->view(u"traits"_s));
    ok(page != nullptr, u"Warfare → Leader traits is the odds page"_s);
    traitodds::OddsView *V = page->odds;
    auto &scripts = win.doc->map<scriptstore::ScriptStore>(u"scripts"_s);
    const traits::Lists before = traits::parse(scripts.text(u"common/traits.txt"_s));
    ok(before.personality.size() > 50 && before.background.size() > 30,
       u"traits: %1 personalities, %2 backgrounds"_s.arg(before.personality.size()).arg(before.background.size()));
    // the odds: attack together sums to 1
    double s = 0;
    for (const auto &[_k, p] : V->chart->both)
        s += p;
    ok(std::abs(s - 1) < 1e-9, u"the together-odds add up to 100 % (%1)"_s.arg(s, 0, 'g', 12));
    shot(u"5 traits odds.png"_s);
    // pick every personality with attack 1, add 1 attack
    V->pick_value(1);
    const long long picked = V->sel.size();
    ok(picked > 0, u"a click on the bar 'attack +1' picks %1 personalities"_s.arg(picked));
    V->value->setText(u"1"_s);
    V->mass_set(u"add"_s);
    QApplication::processEvents();
    const traits::Lists after = traits::parse(scripts.text(u"common/traits.txt"_s));
    long long two = 0;
    for (const auto &t : after.personality)
        two += t.get(u"attack"_s) == 2;
    long long two0 = 0;
    for (const auto &t : before.personality)
        two0 += t.get(u"attack"_s) == 2;
    ok(two == two0 + picked, u"+1 attack: %1 personalities now have attack 2 (before %2)"_s.arg(two).arg(two0));
    // copies: 2 of "relentless"
    V->sel = {u"relentless"_s};
    V->copies->setValue(2);
    V->copy_selected();
    const traits::Lists c = traits::parse(scripts.text(u"common/traits.txt"_s));
    ok(c.find(u"relentless_2"_s) && c.find(u"relentless_3"_s) && c.personality.size() == after.personality.size() + 2,
       u"two copies: relentless_2, relentless_3"_s);
    // delete "relentless": the USA general who has it gets no_personality
    V->sel = {u"relentless"_s};
    V->delete_selected();
    const traits::Lists d = traits::parse(scripts.text(u"common/traits.txt"_s));
    ok(!d.find(u"relentless"_s), u"relentless deleted"_s);
    ok(ob.state(u"USA"_s).leaders[size_t(first_pool)].get(u"personality"_s) == u"no_personality",
       u"…and the general who had it got no_personality"_s);
    V->new_name->setText(u"Iron Will"_s);
    V->new_trait();
    ok(traits::parse(scripts.text(u"common/traits.txt"_s)).find(u"iron_will"_s) != nullptr, u"new trait iron_will"_s);
    V->list_btns[1].second->click();
    QApplication::processEvents();
    shot(u"6 traits backgrounds.png"_s);
    // undo takes the last step back
    win.doc->undo();
    QApplication::processEvents();
    ok(!traits::parse(scripts.text(u"common/traits.txt"_s)).find(u"iron_will"_s), u"Ctrl+Z takes the new trait back"_s);

    // --- Names & colours: rules read from names made of the stats, every name written, a new trait named on its own
    {
        const traits::Lists syn = traits::parse(
            u"personality = {\n\tno_personality = { attack = -1 }\n\ta1 = { attack = 1 }\n\ta2 = { attack = 2 }\n"
            u"\ta3 = { attack = 3 }\n\tm1 = { attack = -1 }\n\tm2 = { attack = -2 }\n\td5 = { defence = 5 }\n}\n"
            u"background = {\n\tno_background = { }\n}\n"_s);
        const QHash<QString, QString> nm{{u"no_personality"_s, u"§WNo Bonus"_s}, {u"a1"_s, u"+1 Attack"_s},
                                         {u"a2"_s, u"+2 Attack"_s},           {u"a3"_s, u"§B+3 Attack"_s},
                                         {u"m1"_s, u"§R-1 Attack"_s},         {u"m2"_s, u"§R-2 Attack"_s},
                                         {u"d5"_s, u"§Y+5 Defence"_s}};
        int n = 0;
        const traitnames::Rules lr = traitnames::learn(syn, [&](const QString &k) { return nm.value(k); }, &n);
        bool same = n == 6;
        QStringList got;
        for (const auto &t : syn.personality) {
            got << traitnames::name_of(t, lr);
            same = same && traitnames::name_of(t, lr) == nm.value(t.key);
        }
        ok(same, u"rules read from stat names give the same names back (%1 rules): %2"_s.arg(lr.rules.size()).arg(got.join(u" | "_s)));
        page->seg[1].second->click();                 // (Names & colours)
        QApplication::processEvents();
        traitnames::NamesView *N = page->names;
        const traits::Lists cur = traits::parse(scripts.text(u"common/traits.txt"_s));
        ok(N->isVisible() && N->preview->topLevelItemCount() == int(cur.personality.size() + cur.background.size()),
           u"Names & colours lists %1 traits"_s.arg(N->preview->topLevelItemCount()));
        N->auto_box->setChecked(true);
        N->write(false);
        QApplication::processEvents();
        auto &loc = win.doc->map<localisation::LocEdits>(u"loc"_s);
        const traits::Trait *some = nullptr;
        for (const auto &t : cur.personality)
            if (t.get(u"attack"_s) < 0)
                some = &t;
        ok(some && loc.text(some->key) == traitnames::name_of(*some, N->rules) && loc.text(some->key)->startsWith(u"§R"_s),
           u"every name written: %1 = %2"_s.arg(some ? some->key : QString(), some ? loc.text(some->key).value_or(QString()) : QString()));
        shot(u"6b trait names.png"_s);
        page->seg[0].second->click();
        QApplication::processEvents();
        V->new_name->setText(u"Bold Stroke"_s);
        V->new_trait();
        V->sel = {u"bold_stroke"_s};
        V->stat_box->setCurrentIndex(0);
        V->value->setText(u"3"_s);
        V->mass_set(u"set"_s);
        QApplication::processEvents();
        ok(loc.text(u"bold_stroke"_s) == u"§G+3 Attack"_s,
           u"a new trait set to attack 3 is named on its own: %1"_s.arg(loc.text(u"bold_stroke"_s).value_or(u"(none)"_s)));
        win.doc->undo();
        win.doc->undo();
        win.doc->undo();
        N->auto_box->setChecked(false);
        QApplication::processEvents();
        ok(!traits::parse(scripts.text(u"common/traits.txt"_s)).find(u"bold_stroke"_s), u"three Ctrl+Z take it all back"_s);
    }

    // --- Warfare → Unit timeline: add a change to an invention, from a click on its line; all land units
    win.war_ws->set_page(u"timeline"_s);
    QApplication::processEvents();
    auto *tv = qobject_cast<unittimeline::UnitTimelineView *>(win.war_ws->view(u"timeline"_s));
    ok(tv != nullptr, u"Warfare → Unit timeline"_s);
    if (!tv->inv_box->isChecked())
        tv->inv_box->click();
    tv->pick(u"artillery"_s, tv->y1);
    QApplication::processEvents();
    long long invs = 0;
    for (int i = 0; i < tv->add_tech->count(); ++i)
        invs += tv->add_tech->itemText(i).contains(u"(invention)"_s);
    auto &te = win.doc->map<techdata::TechEdits>(u"techs"_s);
    ok(invs == te.inventions().size(), u"every invention can be picked: %1 of %2"_s.arg(invs).arg(te.inventions().size()));
    // the list under the box: everything when empty, in sections army, navy, commerce, culture, industry; typing filters
    {
        bool dash = false;
        for (int i = 0; i < tv->add_tech->count(); ++i)
            dash = dash || tv->add_tech->itemText(i).contains(u'—');
        ok(!dash && tv->add_tech->itemText(0).left(4).toInt() > 1700, u"no dashes, the year first: %1"_s.arg(tv->add_tech->itemText(0)));
        tv->add_tech->setEditText(QString());
        tv->tech_pop->open();
        QApplication::processEvents();
        QStringList heads;
        long long rows = 0;
        for (int i = 0; i < tv->tech_pop->list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = tv->tech_pop->list->topLevelItem(i);
            if (it->data(0, Qt::UserRole).toString().isEmpty())
                heads << it->text(0);
            else
                ++rows;
        }
        ok(tv->tech_pop->isVisible() && rows == tv->add_tech->count() &&
               heads.mid(0, 5) == QStringList({u"ARMY"_s, u"NAVY"_s, u"COMMERCE"_s, u"CULTURE"_s, u"INDUSTRY"_s}),
           u"empty box: the whole list, %1 rows in %2"_s.arg(rows).arg(heads.join(u", "_s)));
        tv->tech_pop->grab().save(pypath::join(shots, u"7b tech list.png"_s));
        tv->add_tech->setEditText(u"rifle"_s);
        tv->tech_pop->open();
        QApplication::processEvents();
        long long hits = 0;
        bool all_rifle = true;
        for (int i = 0; i < tv->tech_pop->list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = tv->tech_pop->list->topLevelItem(i);
            if (it->data(0, Qt::UserRole).toString().isEmpty())
                continue;
            ++hits;
            all_rifle = all_rifle && (it->text(1).toLower().contains(u"rifle"_s) ||
                                      it->data(0, Qt::UserRole).toString().contains(u"rifle"_s));
        }
        ok(hits > 0 && hits < rows && all_rifle, u"typing 'rifle': %1 rows"_s.arg(hits));
        tv->tech_pop->grab().save(pypath::join(shots, u"7c tech list rifle.png"_s));
        QKeyEvent ent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(tv->add_tech->lineEdit(), &ent);
        QApplication::processEvents();
        ok(!tv->tech_pop->isVisible() && tv->add_tech->currentText().toLower().contains(u"rifle"_s),
           u"Enter picks it: %1"_s.arg(tv->add_tech->currentText()));
    }
    std::optional<unittimeline::Change> ic;
    for (const auto &x : tv->changes)
        if (x.kind == u"inv" && x.target == u"artillery") {
            ic = x;
            break;
        }
    ok(ic.has_value(), u"artillery has an invention's change: "_s + (ic ? ic->src + u" " + ic->stat : QString()));
    if (ic) {
        QLabel *lab = nullptr;                   // its line in the list: a click on it
        for (QLabel *l : tv->rows_host->findChildren<QLabel *>())
            if (l->text().startsWith(tv->tname(ic->src)) && l->text().contains(u"invention"_s))
                lab = l;
        ok(lab != nullptr, u"its line is listed"_s);
        if (lab) {
            QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(2, 2), lab->mapToGlobal(QPointF(2, 2)), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(lab, &rel);
            QApplication::processEvents();
        }
        ok(tv->add_tech->currentData().toString() == ic->src && tv->add_target->currentData().toString() == u"artillery" &&
               tv->add_stat->currentData().toString() == ic->stat,
           u"the click filled in: %1 / %2 / %3"_s.arg(tv->add_tech->currentText(), tv->add_target->currentText(),
                                                       tv->add_stat->currentText()));
        const QString other = ic->stat == u"attack" ? u"defence"_s : u"attack"_s;
        tv->add_stat->setCurrentIndex(tv->add_stat->findData(other));
        tv->add_val->setText(u"2"_s);
        tv->add_change();
        QApplication::processEvents();
        bool got = false;
        for (const auto &x : tv->changes)
            got |= x.src == ic->src && x.target == u"artillery" && x.stat == other && x.value == 2;
        ok(got, u"a NEW change in %1: artillery %2 +2 (the old one kept)"_s.arg(ic->src, other));
        // all land units
        tv->add_target->setCurrentIndex(tv->add_target->findData(u"army_base"_s));
        tv->add_stat->setCurrentIndex(tv->add_stat->findData(u"attack"_s));
        tv->add_val->setText(u"0.5"_s);
        tv->add_change();
        QApplication::processEvents();
        got = false;
        for (const auto &x : tv->changes)
            got |= x.src == ic->src && x.target == u"army_base" && x.stat == u"attack" && x.value == 0.5;
        ok(got, u"all land units (army_base) attack +0.5 in %1: "_s.arg(ic->src) + tv->warn->text());
    }
    // an invention without an effect block gets one
    for (const QString &k : te.inventions())
        if (!te.text(k).contains(u"effect"_s)) {
            tv->add_tech->setCurrentIndex(tv->add_tech->findData(k));
            tv->add_target->setCurrentIndex(0);
            tv->add_stat->setCurrentIndex(tv->add_stat->findData(u"defence"_s));
            tv->add_val->setText(u"1"_s);
            tv->add_change();
            QApplication::processEvents();
            bool got = false;
            for (const auto &x : tv->changes)
                got |= x.src == k && x.target == u"artillery" && x.stat == u"defence";
            ok(got, u"%1 had no effect block: now it has one with artillery defence +1"_s.arg(k));
            break;
        }
    shot(u"7 unit timeline.png"_s);
    // the graph: three units, attack and defence
    tv->set_view(u"graph"_s);
    tv->set_ticked({u"infantry"_s, u"artillery"_s, u"cavalry"_s});
    tv->_stat_clicked(u"attack"_s);                // (a click: attack alone)
    QApplication::processEvents();
    ok(tv->stats[false] == QStringList{u"attack"_s} && tv->graph->panels.size() == 1 && !tv->graph->whole(),
       u"graph: a click on Attack shows it alone, the years of its changes (%1 – %2)"_s.arg(tv->graph->v0).arg(tv->graph->v1));
    shot(u"7a unit graph.png"_s);
    tv->stats[false] << u"defence"_s;              // (Ctrl+click on Defence)
    tv->_show();
    QApplication::processEvents();
    ok(tv->graph->panels.size() == 2, u"…and Defence added: two charts"_s);
    shot(u"7a2 unit graph two.png"_s);
    tv->set_view(u"lanes"_s);
    QApplication::processEvents();

    // --- Warfare → Unit stats: the conditions (primary culture only)
    win.war_ws->set_page(u"units"_s);
    QApplication::processEvents();
    auto *uv = qobject_cast<unitpanel::UnitStatsView *>(win.war_ws->view(u"units"_s));
    uv->show_unit(u"infantry"_s);
    QApplication::processEvents();
    QCheckBox *pc = nullptr;
    for (QCheckBox *b : uv->findChildren<QCheckBox *>())
        if (b->isVisible() && b->text().contains(u"(primary_culture)"_s))
            pc = b;
    ok(pc && !pc->isChecked(), u"infantry: the tick 'Only from primary-culture pops' is there, off"_s);
    if (pc) {
        pc->click();
        QApplication::processEvents();
        const QString inf = scripts.text(u"units/infantry.txt"_s);
        ok(inf.contains(u"primary_culture = yes"_s), u"a click writes primary_culture = yes into units/infantry.txt"_s);
    }
    uv->show_unit(u"guard"_s);
    QApplication::processEvents();
    for (QCheckBox *b : uv->findChildren<QCheckBox *>())
        if (b->isVisible() && b->text().contains(u"(primary_culture)"_s))
            ok(b->isChecked(), u"guard: primary culture only is ticked (the base game's line)"_s);
    shot(u"8 unit conditions.png"_s);

    // --- Nation: the unciv reforms Japan has at the start; a click gives it land reform
    {
        win.go_page(u"nations"_s);
        win.show_nation(u"JAP"_s);
        QApplication::processEvents();
        nationpanel::NationPanel *np = win.nations_panel;
        auto &nat = win.doc->map<countryhist::NationEdits>(u"nations"_s);
        ok(np->unciv_section->isVisible() && np->unciv_boxes.size() >= 10,
           u"Nation → unciv reforms of Japan: %1 reforms, %2"_s.arg(np->unciv_boxes.size()).arg(np->unciv_title->text()));
        ok(nat.state(u"EGY"_s).reforms.value(u"land_reform"_s) == u"yes_land_reform"_s &&
               !nat.state(u"JAP"_s).reforms.contains(u"admin_reform"_s),
           u"Egypt has land reform in 1836, Japan's reforms come in 1861 (their history)"_s);
        np->unciv_list_btn->setChecked(true);
        QApplication::processEvents();
        np->grab().save(pypath::join(shots, u"9 unciv reforms JAP.png"_s));
        Q_EMIT np->unciv_view->picked(u"land_reform"_s, u"yes_land_reform"_s);
        QApplication::processEvents();
        ok(nat.state(u"JAP"_s).reforms.value(u"land_reform"_s) == u"yes_land_reform"_s && np->unciv_title->text().contains(u"1 of"_s),
           u"a click on an option gives it: %1"_s.arg(np->unciv_title->text()));
    }

    // --- Pictures: the size of a strip's picture, big, and a picture from the computer (C++ only, 2026-11-18)
    {
        win.go_page(u"rules"_s);
        entryview::PagesWorkspace *ws = win.rules_ws;
        ws->set_page(u"values"_s);
        QApplication::processEvents();
        iconboard::BoardPage *bp = static_cast<iconboard::BoardWithList *>(ws->view(u"values"_s))->board;
        const QStringList keys = bp->_order();
        ok(!keys.isEmpty(), u"national values on the board: %1"_s.arg(keys.size()));
        if (!keys.isEmpty()) {
            bp->_pick(keys[0]);
            QApplication::processEvents();
            const auto sz = iconstrips::frame_size(*bp->strips, bp->K.sprite);
            ok(bp->size_lbl && sz && bp->size_lbl->text().contains(u"%1 × %2 pixels"_s.arg(sz->width()).arg(sz->height())),
               u"value picked: the size sign says %1 × %2 pixels"_s.arg(sz ? sz->width() : -1).arg(sz ? sz->height() : -1));
            ok(bp->import_btn && bp->import_btn->isEnabled() && bp->import_btn->text().startsWith(u"Import a picture"_s),
               u"…and offers: "_s + (bp->import_btn ? bp->import_btn->text() : QString()));
            for (int w = 0; w < 5; ++w) {
                QApplication::processEvents();
                QApplication::sendPostedEvents();
            }
            bp->grab().save(pypath::join(shots, u"10 value picture size.png"_s));
            const auto got = bp->node(keys[0]);
            const auto num = bp->icon_of(std::get<0>(*got), std::get<1>(*got), std::get<2>(*got));
            QImage green(sz->width() * 2, sz->height() * 2, QImage::Format_ARGB32);   // (another size: fitted)
            green.fill(QColor(30, 200, 60));
            const size_t u0 = win.doc->undo_stack.size();
            ok(bp->_use_image(keys[0], {keys[0]}, num, false, green, u"check_green.png"_s),
               u"a picture of the computer replaces picture %1"_s.arg(num ? *num : -1));
            QApplication::processEvents();
            const auto fr = iconstrips::frames(*bp->strips, bp->K.sprite);
            const QImage f = num && *num >= 1 && size_t(*num) <= fr.size() ? fr[size_t(*num - 1)].toImage() : QImage();
            ok(!f.isNull() && f.pixelColor(f.width() / 2, f.height() / 2) == QColor(30, 200, 60),
               u"…the strip shows it (green in the middle)"_s);
            ok(win.doc->undo_stack.size() == u0 + 1, u"…one undo step"_s);
            win.undo();
            QApplication::processEvents();
        }
        {
            const QStringList k0 = bp->_order();
            if (bp->board->_boxes.size() > 1) {
                const QPointF c = std::get<0>(bp->board->_boxes[1]).center();
                QMouseEvent ev(QEvent::MouseButtonPress, c, bp->board->mapToGlobal(c), Qt::RightButton, Qt::RightButton,
                               Qt::NoModifier);
                QApplication::sendEvent(bp->board, &ev);
                for (int w = 0; w < 3; ++w)
                    QApplication::processEvents();
            }
            const QStringList k1 = bp->_order();
            ok(k1.size() + 1 == k0.size() && !k1.contains(k0.value(1)),
               u"national values: a right click on %1 deletes it (%2 left)"_s.arg(k0.value(1)).arg(k1.size()));
            win.undo();
            QApplication::processEvents();
            ok(bp->_order() == k0, u"…Ctrl+Z brings it back"_s);
        }
        // focuses: the size and "as a new picture of the strip"
        ws->set_page(u"focus"_s);
        QApplication::processEvents();
        iconboard::BoardPage *fp = static_cast<iconboard::BoardWithList *>(ws->view(u"focus"_s))->board;
        const QStringList fk = fp->_order();
        if (!fk.isEmpty()) {
            fp->_pick(fk[0]);
            QApplication::processEvents();
            const auto sz = iconstrips::frame_size(*fp->strips, fp->K.sprite);
            const size_t n0 = iconstrips::frames(*fp->strips, fp->K.sprite).size();
            ok(fp->size_lbl && sz && fp->size_lbl->text().contains(u"pixels"_s) && fp->import_btn &&
                   fp->import_btn->text() == u"Import a picture for this national focus",
               u"focus picked: %1 × %2 pixels, “%3”"_s.arg(sz ? sz->width() : -1).arg(sz ? sz->height() : -1).arg(
                   fp->import_btn ? fp->import_btn->text() : QString()));
            for (int w = 0; w < 5; ++w) {
                QApplication::processEvents();
                QApplication::sendPostedEvents();
            }
            fp->grab().save(pypath::join(shots, u"11 focus picture size.png"_s));
            const auto got = fp->node(fk[0]);
            const auto num = fp->icon_of(std::get<0>(*got), std::get<1>(*got), std::get<2>(*got));
            const QStringList path = std::get<0>(*got).isEmpty() ? QStringList{fk[0]} : QStringList{std::get<0>(*got), fk[0]};
            // vanilla: every focus shows its own picture -> the import replaces it; a picture another one shows -> new
            ok(!fp->_shared(fk[0], num) && fp->_shared(fk[0], std::nullopt),
               u"…its picture is its own (the import replaces it); none of its own -> a new one"_s);
            {   // the pictures into one's library: this one, then the whole strip
                const QString lib_dir = moreviews::library_dir(u"pictures"_s);
                const QStringList before = QDir(lib_dir).entryList({u"*.png"_s}, QDir::Files);
                fp->lib_one_btn->click();
                QApplication::processEvents();
                QStringList now = QDir(lib_dir).entryList({u"*.png"_s}, QDir::Files);
                QStringList mine;
                for (const QString &f : now)
                    if (!before.contains(f))
                        mine << f;
                ok(mine.size() == 1 && mine[0].contains(u"Build Railroads"_s),
                   u"“Put this picture into my library”: %1"_s.arg(mine.join(u", "_s)));
                fp->lib_all_btn->click();
                QApplication::processEvents();
                now = QDir(lib_dir).entryList({u"*.png"_s}, QDir::Files);
                const qsizetype all = now.size() - before.size() - 1;
                ok(all == static_cast<qsizetype>(n0), u"“%1”: %2 files"_s.arg(fp->lib_all_btn->text()).arg(all));
                ok(fp->lib->count() >= static_cast<int>(n0) + 1, u"…and in the library list (%1)"_s.arg(fp->lib->count()));
                fp->grab().save(pypath::join(shots, u"11b focus library.png"_s));
                // (the check's own files out again: the check's library is the build folder's)
                for (const QString &f : now)
                    if (!before.contains(f))
                        QFile::remove(QDir(lib_dir).filePath(f));
            }
            QImage blue(sz->width(), sz->height(), QImage::Format_ARGB32);
            blue.fill(QColor(40, 60, 220));
            ok(fp->_use_image(fk[0], path, num, true, blue, u"check_blue.png"_s), u"…a new picture of the strip"_s);
            QApplication::processEvents();
            const size_t n1 = iconstrips::frames(*fp->strips, fp->K.sprite).size();
            ok(n1 == n0 + 1, u"…the strip has %1 pictures (was %2)"_s.arg(n1).arg(n0));
            win.undo();
            QApplication::processEvents();
            ok(iconstrips::frames(*fp->strips, fp->K.sprite).size() == n0, u"…undone"_s);
        }
        // building types: railroad's levels, a picture as a new level, the max level follows on a click
        ws->set_page(u"buildings"_s);
        QApplication::processEvents();
        buildtypes::BuildTypesPanel *bt = win.bt_panel;
        for (int i = 0; i < bt->tree->topLevelItemCount(); ++i)
            if (bt->tree->topLevelItem(i)->text(0).contains(u"ailroad"_s) ||
                bt->tree->topLevelItem(i)->data(0, Qt::UserRole).toString() == u"railroad")
                bt->tree->setCurrentItem(bt->tree->topLevelItem(i));
        QApplication::processEvents();
        ok(bt->cur == u"railroad", u"Building types: "_s + bt->cur.value_or(QString()));
        const int n0 = bt->frames_n;
        const long long mx0 = bt->max_level->value();
        ok(bt->size_lbl->text().contains(u" pixels"_s) && bt->imp_rep_btn->isEnabled() &&
               bt->imp_rep_btn->text() == u"Import a picture for this building level",
           u"…the size sign: %1"_s.arg(QString(bt->size_lbl->text()).remove(QRegularExpression(u"<[^>]*>"_s)).left(60)));
        ok(!bt->lv_btn->isVisible(), u"…%1 pictures, max level %2: no level button"_s.arg(n0).arg(mx0));
        ok(bt->lvl_err->isVisibleTo(bt) && bt->lvl_err->text().contains(u"Too few levels by technology"_s),
           u"…red: "_s + bt->lvl_err->text().left(90));
        QImage gold(40, 32, QImage::Format_ARGB32);
        gold.fill(QColor(220, 180, 40));
        // ＋ after the last picture: a new level, the max level with it, one step
        QToolButton *plus = nullptr;
        for (QToolButton *tb : bt->frames_host->findChildren<QToolButton *>())
            if (tb->text() == u"new level" && tb->isVisible())
                plus = tb;
        ok(plus != nullptr, u"＋ “new level” after the pictures"_s);
        const size_t u0 = win.doc->undo_stack.size();
        if (plus)
            plus->click();
        QApplication::processEvents();
        ok(bt->frames_n == n0 + 1 && bt->max_level->value() == mx0 + 1 && bt->frame_sel == n0 && !bt->lv_btn->isVisible() &&
               win.doc->undo_stack.size() == u0 + 1,
           u"＋: %1 pictures, max level %2, level %3 picked, one step — “%4”"_s.arg(bt->frames_n).arg(
               bt->max_level->value()).arg(bt->frame_sel).arg(bt->frame_note->text().left(60)));
        for (int w = 0; w < 5; ++w) {
            QApplication::processEvents();
            QApplication::sendPostedEvents();
        }
        bt->grab().save(pypath::join(shots, u"12 railroad new level.png"_s));
        // a picture dragged: level 1's picture to level 3
        {
            const auto im0 = iconstrips::strips_for(*win.mod, *win.doc).image(bt->frame_sprite);
            const int w0 = im0 ? im0->width() / bt->frames_n : 0;
            const QImage one = im0 ? im0->copy(w0, 0, w0, im0->height()) : QImage();
            bt->_frame_move(1, 3);
            QApplication::processEvents();
            const auto im1 = iconstrips::strips_for(*win.mod, *win.doc).image(bt->frame_sprite);
            ok(im1 && !one.isNull() && im1->copy(3 * w0, 0, w0, im1->height()).convertToFormat(QImage::Format_ARGB32) ==
                                           one.convertToFormat(QImage::Format_ARGB32) && bt->frame_sel == 3,
               u"level 1's picture dragged to level 3"_s);
        }
        // the level pictures into one's library
        {
            const QString lib_dir = moreviews::library_dir(u"pictures"_s);
            const QStringList before = QDir(lib_dir).entryList({u"*.png"_s}, QDir::Files);
            bt->lib_all_btn->click();
            QApplication::processEvents();
            const QStringList now = QDir(lib_dir).entryList({u"*.png"_s}, QDir::Files);
            ok(now.size() - before.size() == bt->frames_n && bt->lib->findText(u"level 0"_s, Qt::MatchContains) >= 0,
               u"“%1”: %2 files, in the library list"_s.arg(bt->lib_all_btn->text()).arg(now.size() - before.size()));
            for (const QString &f : now)
                if (!before.contains(f))
                    QFile::remove(QDir(lib_dir).filePath(f));
        }
        bt->frame_sel = 2;
        bt->_frame_edit(0, gold);
        QApplication::processEvents();
        const auto img = iconstrips::strips_for(*win.mod, *win.doc).image(bt->frame_sprite);
        const int fw = img ? img->width() / bt->frames_n : 0;
        ok(img && img->pixelColor(2 * fw + fw / 2, img->height() / 2) == QColor(220, 180, 40),
           u"picture of level 2 replaced (strip %1 px wide)"_s.arg(img ? img->width() : -1));
        bt->grab().save(pypath::join(shots, u"13 railroad replaced.png"_s));
        win.undo();
        win.undo();
        win.undo();
        QApplication::processEvents();
        const auto im2 = iconstrips::strips_for(*win.mod, *win.doc).image(bt->frame_sprite);
        ok(bt->frames_n == n0 && bt->max_level->value() == mx0 && im2 && im2->width() == n0 * 40,
           u"…all three undone (%1 pictures, max level %2)"_s.arg(bt->frames_n).arg(bt->max_level->value()));
        // a level deleted: right click on level 6's picture, then the button on level 1
        {
            QToolButton *six = nullptr;
            for (QToolButton *tb : bt->frames_host->findChildren<QToolButton *>())
                if (tb->property("level").toInt() == 6 && tb->isVisible())
                    six = tb;
            const size_t u1 = win.doc->undo_stack.size();
            if (six) {
                QMouseEvent ev(QEvent::MouseButtonPress, QPointF(5, 5), six->mapToGlobal(QPointF(5, 5)), Qt::RightButton,
                               Qt::RightButton, Qt::NoModifier);
                QApplication::sendEvent(six, &ev);
            }
            for (int w = 0; w < 3; ++w)
                QApplication::processEvents();
            ok(six && bt->frames_n == n0 - 1 && bt->max_level->value() == mx0 - 1 && win.doc->undo_stack.size() == u1 + 1 &&
                   !bt->lvl_err->isVisibleTo(bt),
               u"right click on level 6: %1 pictures, highest level %2, one step, no red message (techs reach 5)"_s.arg(
                   bt->frames_n).arg(bt->max_level->value()));
            ok(bt->cut_btn->text() == u"Delete this building level", u"the button: "_s + bt->cut_btn->text());
            bt->frame_sel = 1;
            bt->cut_btn->click();
            QApplication::processEvents();
            ok(bt->frames_n == n0 - 2 && bt->max_level->value() == mx0 - 2 && bt->lvl_err->isVisibleTo(bt) &&
                   bt->lvl_err->text().contains(u"Too many levels by technology"_s),
               u"“Delete this building level” on level 1: highest level %1, red: %2"_s.arg(bt->max_level->value()).arg(
                   bt->lvl_err->text().left(90)));
            for (int w = 0; w < 5; ++w) {
                QApplication::processEvents();
                QApplication::sendPostedEvents();
            }
            bt->grab().save(pypath::join(shots, u"14 railroad level deleted.png"_s));
            win.undo();
            win.undo();
            QApplication::processEvents();
            ok(bt->frames_n == n0 && bt->max_level->value() == mx0, u"…both undone"_s);
        }
    }

    // --- Apply
    const bool saved = win.save(false);
    ok(saved, u"Apply: "_s + win.st_msg->text());
    QStringList written;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        written << QDir(root).relativeFilePath(it.next());
    written.sort();
    ok(written.contains(u"common/traits.txt"_s), u"written: "_s + written.join(u", "_s).left(400));
    const QString t2 = script::read_text(pypath::join(root, u"common/traits.txt"_s));
    ok(t2.contains(u"relentless_2 = {"_s) && !t2.contains(u"\trelentless = {"_s), u"traits.txt has the copies, not the deleted one"_s);
    QString usa_file;
    for (const QString &w : written)
        if (w.startsWith(u"history/units/"_s) && w.contains(u"USA"_s))
            usa_file = w;
    const QString u2 = usa_file.isEmpty() ? QString() : script::read_text(pypath::join(root, usa_file));
    ok(u2.contains(u"name = \"Check General\""_s) && u2.contains(u"name = \"New general\""_s),
       u"%1 has the renamed and the new general"_s.arg(usa_file));
    const oob::OOB re = oob::parse_oob(u2, {});
    ok(re.leaders.size() == n0 + 1, u"read again: %1 leaders in the pool"_s.arg(re.leaders.size()));
    QString jap_file;
    for (const QString &w : written)
        if (w.startsWith(u"history/countries/JAP"_s))
            jap_file = w;
    const QString j2 = jap_file.isEmpty() ? QString() : script::read_text(pypath::join(root, jap_file));
    ok(j2.contains(u"\nland_reform = yes_land_reform"_s) && j2.contains(u"\tadmin_reform = yes_admin_reform"_s),
       u"%1 has the land reform from the start (and its 1861 reforms still)"_s.arg(jap_file));
    std::printf("%s\n", fails ? "FAILED" : "PASS");
    std::fflush(stdout);
    std::_Exit(fails ? 1 : 0);
}
